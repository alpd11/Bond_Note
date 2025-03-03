const express = require("express");
const { WebSocketServer } = require("ws");
require("dotenv").config();

const PORT = process.env.PORT || 3000;
const app = express();
const server = app.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
});

// Serve a basic message for browser visits
app.get("/", (req, res) => {
    res.send("WebSocket Server is Running!");
});

// WebSocket Server
const wss = new WebSocketServer({ server });

wss.on("connection", (ws) => {
    console.log("New client connected");

    ws.on("message", (data) => {
        console.log("Received:", data.toString());

        // Broadcast message to all connected clients
        wss.clients.forEach((client) => {
            if (client !== ws && client.readyState === 1) {
                client.send(data);
            }
        });
    });

    ws.on("close", () => console.log("Client disconnected"));
});
