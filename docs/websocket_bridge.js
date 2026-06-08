#!/usr/bin/env node
/**
 * WebSocket-to-TCP Bridge + Static HTTP Server for Seer Battle Simulator
 *
 * Bridges browser WebSocket connections to the TCP battle server.
 * Also serves battle-debug.html and battle.html over HTTP.
 *
 * Usage:
 *   node websocket_bridge.js [tcp_port] [ws_port]
 *
 * Default: TCP port 4399, HTTP+WS port 8080
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer, WebSocket } = require('ws');
const net = require('net');

// Configuration
const TCP_PORT = parseInt(process.argv[2] || '4399', 10);
const SERVE_PORT = parseInt(process.argv[3] || '8080', 10);
const TCP_HOST = '127.0.0.1';

// Protocol constants (must match server)
const Command = {
    INVALID: 0,
    SELECT_SKILL: 1,
    USE_MEDICINE: 2,
    CHOOSE_PET: 3,
    SEND_EMOJI: 4,
    INIT_BATTLE: 5,
    SYNC_STATE: 10,
    HEARTBEAT: 11,
    DEBUG_STEP: 20,
    DEBUG_CONTINUE: 21,
    DEBUG_BREAKPOINT: 22,
    DEBUG_FULLSTATE: 23,
};

const CMD_NAME = {};
for (const [k, v] of Object.entries(Command)) CMD_NAME[v] = k;

// Create HTTP server that serves static files
const server = http.createServer((req, res) => {
    let filePath = req.url === '/' ? '/battle-debug.html' : req.url;
    const fullPath = path.join(__dirname, filePath);

    // Security: only serve .html, .js, .css files from docs directory
    const ext = path.extname(fullPath);
    if (!['.html', '.js', '.css'].includes(ext)) {
        res.writeHead(404);
        res.end('Not Found');
        return;
    }

    fs.readFile(fullPath, (err, data) => {
        if (err) {
            res.writeHead(404);
            res.end('Not Found');
            return;
        }
        const mimeTypes = { '.html': 'text/html; charset=utf-8', '.js': 'application/javascript', '.css': 'text/css' };
        res.writeHead(200, { 'Content-Type': mimeTypes[ext] || 'text/plain' });
        res.end(data);
    });
});

// Create WebSocket server attached to HTTP server
const wss = new WebSocketServer({ server });

console.log(`Battle Debug Bridge Started`);
console.log(`   HTTP:     http://localhost:${SERVE_PORT}/battle-debug.html`);
console.log(`   HTTP:     http://localhost:${SERVE_PORT}/battle.html`);
console.log(`   WebSocket: ws://localhost:${SERVE_PORT}`);
console.log(`   TCP Server: ${TCP_HOST}:${TCP_PORT}`);
console.log('');

// Handle WebSocket connections from browsers
wss.on('connection', (ws, req) => {
    const clientIp = req.socket.remoteAddress;
    console.log(`Browser connected: ${clientIp}`);

    let tcpConnection = null;

    // Helper to build binary message for TCP server
    function buildTcpMessage(command, uuid, params) {
        const paramLen = params ? params.length : 0;
        const msgLen = 10 + paramLen;
        const buffer = Buffer.alloc(msgLen);
        buffer.writeUInt32BE(msgLen, 0);
        buffer.writeUInt16BE(command, 4);
        buffer.writeUInt32BE(uuid, 6);
        if (params) params.copy(buffer, 10);
        return buffer;
    }

    // Helper to parse TCP message header
    function parseTcpHeader(buffer) {
        if (buffer.length < 10) return null;
        return {
            totalLength: buffer.readUInt32BE(0),
            command: buffer.readUInt16BE(4),
            uuid: buffer.readUInt32BE(6),
        };
    }

    // Forward WebSocket message to TCP server
    ws.on('message', (data) => {
        if (!tcpConnection || !tcpConnection.connected) {
            console.log('TCP not connected, cannot forward message');
            return;
        }

        try {
            const msgBuffer = Buffer.from(data);
            if (msgBuffer.length >= 10) {
                const header = parseTcpHeader(msgBuffer);
                const cmdName = CMD_NAME[header.command] || 'UNKNOWN';
                console.log(`Client -> TCP: cmd=${cmdName}(${header.command}), uuid=${header.uuid}`);
                tcpConnection.write(msgBuffer);
            }
        } catch (err) {
            console.error('Error forwarding message to TCP:', err.message);
        }
    });

    // Handle WebSocket close
    ws.on('close', () => {
        console.log(`Browser disconnected: ${clientIp}`);
        if (tcpConnection) {
            tcpConnection.destroy();
            tcpConnection = null;
        }
    });

    ws.on('error', (err) => {
        console.error(`WebSocket error: ${err.message}`);
    });

    // Connect to TCP server
    console.log(`Connecting to TCP server ${TCP_HOST}:${TCP_PORT}...`);
    tcpConnection = new net.Socket();

    tcpConnection.connect(TCP_PORT, TCP_HOST, () => {
        console.log(`Connected to TCP server`);
    });

    // Buffer for incoming TCP data
    let tcpBuffer = Buffer.alloc(0);

    // Handle data from TCP server
    tcpConnection.on('data', (data) => {
        tcpBuffer = Buffer.concat([tcpBuffer, data]);

        while (tcpBuffer.length >= 10) {
            const header = parseTcpHeader(tcpBuffer);
            if (!header || tcpBuffer.length < header.totalLength) break;

            const cmdName = CMD_NAME[header.command] || 'UNKNOWN';
            const msgBody = tcpBuffer.slice(10, header.totalLength);

            // Log JSON payloads
            if (msgBody.length > 0) {
                const text = msgBody.toString('utf8').replace(/\0/g, '').trim();
                if (text.startsWith('{')) {
                    const preview = text.length > 200 ? text.substring(0, 200) + '...' : text;
                    console.log(`TCP -> Client: cmd=${cmdName}(${header.command}) | ${preview}`);
                } else {
                    console.log(`TCP -> Client: cmd=${cmdName}(${header.command}), uuid=${header.uuid}, bytes=${msgBody.length}`);
                }
            } else {
                console.log(`TCP -> Client: cmd=${cmdName}(${header.command}), uuid=${header.uuid}`);
            }

            // Forward raw TCP frame to WebSocket client (include header)
            const fullFrame = tcpBuffer.slice(0, header.totalLength);
            if (ws.readyState === WebSocket.OPEN) {
                ws.send(fullFrame);
            }

            tcpBuffer = tcpBuffer.slice(header.totalLength);
        }
    });

    tcpConnection.on('close', () => {
        console.log(`TCP connection closed`);
        if (ws.readyState === WebSocket.OPEN) ws.close();
    });

    tcpConnection.on('error', (err) => {
        console.error(`TCP error: ${err.message}`);
    });
});

// Start HTTP server
server.listen(SERVE_PORT, () => {
    console.log(`Server listening on http://localhost:${SERVE_PORT}`);
    console.log(`Open http://localhost:${SERVE_PORT}/battle-debug.html for debug console`);
    console.log('');
});
