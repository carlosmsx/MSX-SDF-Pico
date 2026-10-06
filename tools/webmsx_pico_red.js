// Pico simulada con red para probar GET.COM en WebMSX (ver tools/prepara_webmsx.py).
// Se pega en la consola despues de tools/webmsx_arnes.js: conecta los puertos
// 00h y 01h a un SDF-Pico de mentira que contesta CMD_NETCONN y CMD_NETIO
// (docs/RED.md) con un servidor HTTP falso. Las rutas estan en red.rutas:
// cada una devuelve {estado, encabezados, cuerpo} y opcionalmente corte (cuantos
// bytes del cuerpo manda antes de cerrar). El host "noexiste" no conecta.
//
//   boot(); typ("b:\r", 100); typ("get http://srv/datos.bin\r", 3000)
//   red.pedidos               los pedidos HTTP que llegaron
//   leerArchivo(1, "DATOS.BIN")   el archivo del disco B:, como Uint8Array

(function () {
    const enc = s => new TextEncoder().encode(s);
    const azar = (n, semilla) => {
        const b = new Uint8Array(n);
        let x = semilla;
        for (let i = 0; i < n; i++) { x = (x * 1103515245 + 12345) >>> 0; b[i] = x >>> 24; }
        return b;
    };

    const R = window.red = {
        cmd: 0, par: [], q: [], busy: 0,
        estado: 0, polls: 0, host: '', puerto: 0,
        tx: [], rx: [], pedidos: [], respondido: false,
        rutas: {
            '/dir/datos.bin': () => ({ estado: '200 OK', cuerpo: azar(30000, 7) }),
            '/': () => ({ estado: '200 OK', cl: false, cuerpo: enc('<html>hola MSX</html>\r\n'.repeat(50)) }),
            '/nada': () => ({ estado: '404 Not Found', cuerpo: enc('no esta') }),
            '/viejo': () => ({ estado: '301 Moved Permanently', encabezados: ['Location: http://srv/nuevo'], cuerpo: enc('') }),
            '/corto': () => ({ estado: '200 OK', cuerpo: azar(5000, 9), corte: 3000 }),
        },
    };
    R.esperado = p => R.rutas[p]().cuerpo;

    function responder(pedido) {
        R.pedidos.push(pedido);
        const ruta = (pedido.split(' ')[1] || '').split('?')[0];
        const f = R.rutas[ruta] || (() => ({ estado: '404 Not Found', cuerpo: enc('?') }));
        const r = f();
        let h = 'HTTP/1.1 ' + r.estado + '\r\nServer: falso\r\n';
        if (r.cl !== false)
            h += 'Content-Length: ' + r.cuerpo.length + '\r\n';
        for (const e of r.encabezados || [])
            h += e + '\r\n';
        h += '\r\n';
        const cuerpo = r.corte !== undefined ? r.cuerpo.slice(0, r.corte) : r.cuerpo;
        R.rx = [...enc(h), ...cuerpo];
        R.respondido = true;
    }

    function ejecutar() {
        R.busy = 5;
        const p = R.par;
        if (R.cmd === 0xB1) {               // CMD_NETCONN
            const n = p[2];
            R.host = String.fromCharCode(...p.slice(3, 3 + n));
            R.puerto = p[0] | (p[1] << 8);
            R.tx = []; R.rx = []; R.respondido = false;
            R.estado = n ? 2 : 0;           // CONECTANDO
            R.polls = 3;
            R.q = [0];
        } else if (R.cmd === 0xB2) {        // CMD_NETIO
            const max = p[0], n = p[1];
            if (R.estado === 2 && --R.polls <= 0)
                R.estado = R.host === 'noexiste' ? 4 : 1;
            if (R.estado === 1 && n) {
                R.tx.push(...p.slice(2, 2 + n));
                const t = String.fromCharCode(...R.tx);
                if (t.endsWith('\r\n\r\n')) responder(t);
            }
            // de a pedazos de largo variable, para probar los bordes
            const m = Math.min(max, R.rx.length, 1 + Math.floor(Math.random() * 255));
            const out = R.rx.splice(0, m);
            if (R.estado === 1 && R.respondido && !R.rx.length)
                R.estado = 4;               // el servidor cierra despues del ultimo byte
            R.q = [R.estado, m, ...out];
        }
    }

    function completo() {
        const p = R.par;
        if (R.cmd === 0xB1) return p.length >= 3 && p.length >= 3 + p[2];
        if (R.cmd === 0xB2) return p.length >= 2 && p.length >= 2 + p[1];
        return true;
    }

    const bus = WMSX.room.machine.bus;
    bus.connectOutputDevice(0x01, v => { R.cmd = v; R.par = []; R.q = []; if (completo()) ejecutar(); });
    bus.connectOutputDevice(0x00, v => { R.par.push(v); if (completo()) ejecutar(); });
    bus.connectInputDevice(0x01, () => R.busy > 0 ? (R.busy--, 0xC0) : 0x40);
    bus.connectInputDevice(0x00, () => R.q.length ? R.q.shift() : 0xFF);

    // Un archivo de la imagen FAT12 de la unidad (0 = A:), o null
    window.leerArchivo = function (unidad, nombre) {
        const d = WMSX.room.diskDrive.getDriveStack(unidad)[0].content;
        const w = i => d[i] | (d[i + 1] << 8);
        const bps = w(11), spc = d[13], rsv = w(14), nfat = d[16], nroot = w(17), spf = w(22);
        const raiz = (rsv + nfat * spf) * bps, datos = raiz + nroot * 32;
        const [b, e] = nombre.toUpperCase().split('.');
        const buscado = b.padEnd(8) + (e || '').padEnd(3);
        for (let i = 0; i < nroot; i++) {
            const o = raiz + i * 32;
            if (d[o] === 0) break;
            if (String.fromCharCode(...d.slice(o, o + 11)) !== buscado) continue;
            let cl = w(o + 26), tam = d[o + 28] | (d[o + 29] << 8) | (d[o + 30] << 16) | (d[o + 31] << 24);
            const out = [];
            while (cl >= 2 && cl < 0xFF8 && out.length < tam) {
                const s = datos + (cl - 2) * spc * bps;
                out.push(...d.slice(s, s + spc * bps));
                const f = rsv * bps + Math.floor(cl * 3 / 2);
                cl = cl & 1 ? w(f) >> 4 : w(f) & 0xFFF;
            }
            return new Uint8Array(out.slice(0, tam));
        }
        return null;
    };
    window.igual = (a, b) => a && b && a.length === b.length && a.every((x, i) => x === b[i]);
})();
