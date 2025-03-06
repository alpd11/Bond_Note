const express = require("express");
const { WebSocketServer } = require("ws");
const fs = require("fs");
const path = require("path");
require("dotenv").config();

const PORT = process.env.PORT || 3000;
const app = express();
const server = app.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
});

// Serve a basic response for browser requests
app.get("/", (req, res) => {
    res.send("WebSocket Server is Running! JSON data is stored.");
});

// WebSocket Server
const wss = new WebSocketServer({ server });

wss.on("connection", (ws) => {
    console.log("New client connected");

    ws.on("message", (data) => {
        console.log("Received:", data.toString());

        try {
            const jsonData = JSON.parse(data);
            saveJsonToFile(jsonData); // Save JSON data
        } catch (error) {
            console.error("Invalid JSON format:", error);
        }

        // Broadcast message to all connected clients
        wss.clients.forEach((client) => {
            if (client !== ws && client.readyState === 1) {
                client.send(data);
            }
        });
    });

    ws.on("close", () => console.log("Client disconnected"));
});

// Function to save JSON data to a file
function saveJsonToFile(jsonData) {
    const filePath = path.join(__dirname, "stored_data.json");

    // Read existing data if file exists
    let existingData = [];
    if (fs.existsSync(filePath)) {
        const fileContents = fs.readFileSync(filePath, "utf-8");
        existingData = fileContents ? JSON.parse(fileContents) : [];
    }

    // Append new data
    existingData.push(jsonData);

    // Save updated data back to file
    fs.writeFileSync(filePath, JSON.stringify(existingData, null, 2), "utf-8");
    console.log("JSON Data saved to stored_data.json");
}
