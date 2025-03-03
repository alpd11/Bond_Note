const express = require("express");
const { WebSocketServer } = require("ws");
require("dotenv").config();

const PORT = process.env.PORT || 3000;
const app = express();
const server = app.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
});

// WebSocket Server
const wss = new WebSocketServer({ server });

wss.on("connection", (ws) => {
    console.log("New client connected");

    ws.on("message", (data) => {
        console.log("Received:", data.toString());

        // Broadcast to all clients
        wss.clients.forEach((client) => {
            if (client !== ws && client.readyState === 1) {
                client.send(data);
            }
        });
    });

    ws.on("close", () => console.log("Client disconnected"));
});
