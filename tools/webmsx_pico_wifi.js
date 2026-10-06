// Pico simulada para probar WIFI.COM en WebMSX (ver tools/prepara_webmsx.py).
// Se pega en la consola despues de tools/webmsx_arnes.js: conecta los puertos
// 00h y 01h a un SDF-Pico de mentira que contesta CMD_WIFISTAT, CMD_WIFISCAN,
// CMD_WIFIREDES y CMD_WIFIJOIN (docs/WIFI.md). La busqueda tarda 5 consultas
// y la conexion 6; conecta con la clave "clave1234" o con una red abierta.
//
//   boot(); typ("b:\r", 100); typ("wifi\r", 600); typ("2\r", 100); typ("clave1234\r", 900)
//   pico.log       lo que llego por CMD_WIFIJOIN

(function () {
    const P = window.pico = {
        cmd: 0, par: [], q: [], busy: 0, log: [],
        estado: 4, red: 'Casa', ip: '', resultado: 1, motivo: '',
        scanPolls: 0, joinPolls: -1, join: null,
        redes: [
            { ssid: 'Casa', rssi: -48, f: 2 },          // f: 1 abierta, 2 guardada
            { ssid: 'Vecino$5', rssi: -71, f: 0 },
            { ssid: 'Caf\u00e9 Libre', rssi: -8, f: 1 },
        ],
    };
    const z = s => [...new TextEncoder().encode(s), 0];

    function ejecutar() {
        const q = [];
        P.busy = 30;                        // consultas con OCUPADO
        switch (P.cmd) {
        case 0xB5:
            if (P.joinPolls >= 0 && --P.joinPolls < 0) {
                if (P.join.key === 'clave1234' || P.join.key === '') {
                    P.resultado = 1; P.estado = 3; P.red = P.join.ssid; P.ip = '192.168.0.37';
                } else {
                    P.resultado = -1; P.motivo = 'clave incorrecta'; P.estado = 4;
                }
            }
            q.push(P.estado, P.resultado == 0 ? 0 : P.resultado > 0 ? 1 : 2,
                   P.estado == 3 ? (-52 & 0xFF) : 0, ...z(P.red),
                   ...z(P.estado == 3 ? P.ip : ''), ...z(P.resultado < 0 ? P.motivo : ''));
            break;
        case 0xB6:
            P.scanPolls = 5;
            q.push(0);
            break;
        case 0xB7:
            if (P.scanPolls > 0) { P.scanPolls--; q.push(1, 0); break; }
            q.push(0, P.redes.length);
            for (const r of P.redes)
                q.push(r.rssi & 0xFF, r.f, ...z(r.ssid));
            break;
        case 0xB8: {
            const p = P.par, ns = p[0], nk = p[1 + ns];
            const ssid = new TextDecoder().decode(new Uint8Array(p.slice(1, 1 + ns)));
            const key = String.fromCharCode(...p.slice(2 + ns, 2 + ns + nk));
            P.join = { ssid, key };
            P.log.push('JOIN ' + JSON.stringify(P.join));
            P.resultado = 0; P.estado = 2; P.red = ssid; P.joinPolls = 6;
            q.push(0);
            break;
        }
        }
        P.q = q;
    }

    function completo() {
        if (P.cmd !== 0xB8)
            return true;
        const p = P.par;
        return p.length > 0 && p.length > 1 + p[0] && p.length >= 2 + p[0] + p[1 + p[0]];
    }

    const bus = WMSX.room.machine.bus;
    bus.connectOutputDevice(0x01, v => { P.cmd = v; P.par = []; P.q = []; if (completo()) ejecutar(); });
    bus.connectOutputDevice(0x00, v => { P.par.push(v); if (completo()) ejecutar(); });
    bus.connectInputDevice(0x01, () => P.busy > 0 ? (P.busy--, 0xC0) : 0x40);
    bus.connectInputDevice(0x00, () => P.q.length ? P.q.shift() : 0xFF);
})();
