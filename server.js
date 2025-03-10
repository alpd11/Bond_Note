const express = require("express");
const { WebSocketServer } = require("ws");
const mongoose = require("mongoose");
require("dotenv").config();

const PORT = process.env.PORT || 3000;
const app = express();
const server = app.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
});

// Connect to MongoDB
mongoose.connect(process.env.MONGO_URI, { useNewUrlParser: true, useUnifiedTopology: true })
    .then(() => console.log("MongoDB Connected"))
    .catch(err => console.error("MongoDB Connection Error:", err));

// Define Schema
const StrokeSchema = new mongoose.Schema({
    stroke: Number,  // 笔画编号
    points: [        // 点集合
        {
            x: Number,  // X 坐标
            y: Number   // Y 坐标
        }
    ]
});

const DataSchema = new mongoose.Schema({
    strokes: [StrokeSchema],  // 笔画数组
    timestamp: { type: Date, default: Date.now }  // 时间戳
});

const DataModel = mongoose.model("Data", DataSchema);

// Store connected clients and their MAC addresses
const clients = new Map();

// Serve a basic message for browser requests
app.get("/", (req, res) => {
    res.send("WebSocket Server is Running! JSON data is stored in MongoDB.");
});

// WebSocket Server
const wss = new WebSocketServer({ server });

wss.on("connection", (ws) => {
    console.log("New client attempting connection...");

    // Wait for the first message to get the MAC address
    ws.once("message", (data) => {
        try {
            const jsonData = JSON.parse(data);

            // Ensure the message contains a MAC address
            if (!jsonData.mac) {
                console.error("No MAC address provided. Closing connection.");
                ws.close();
                return;
            }

            const macAddress = jsonData.mac;

            // Check if the MAC address already exists in the client list
            if (clients.has(macAddress)) {
                let existingClient = clients.get(macAddress);

                // If the existing WebSocket connection is still open, prevent duplicate registration
                if (existingClient && existingClient.readyState === 1) {
                    console.warn(`Device with MAC ${macAddress} is already connected.`);
                    ws.send(JSON.stringify({ error: "Device already connected" }));
                    ws.close();
                    return;
                } else {
                    console.log(`Re-registering device with MAC ${macAddress} (Previous session lost).`);
                    clients.delete(macAddress); // Remove stale connection
                }
            }

            // Register the new connection
            clients.set(macAddress, ws);
            console.log(`Device with MAC ${macAddress} successfully connected.`);

            // Handle incoming messages
            ws.on("message", (message) => {
                console.log(`Received from ${macAddress}:`, message.toString());

                try {
                    const parsedData = JSON.parse(message);
                    parsedData.mac = macAddress; // Attach MAC address to data
                    saveJsonToDatabase(parsedData); // Save to database
                } catch (error) {
                    console.error("Invalid JSON format:", error);
                }

                // Broadcast message to all connected clients except sender
                wss.clients.forEach((client) => {
                    if (client !== ws && client.readyState === 1) {
                        client.send(message);
                    }
                });
            });

            // Handle disconnection properly
            ws.on("close", () => {
                // Only remove the client if it is still in the list
                if (clients.get(macAddress) === ws) {
                    clients.delete(macAddress);
                    console.log(`Device with MAC ${macAddress} disconnected.`);
                }
            });

            ws.on("error", (err) => {
                console.error(`WebSocket error for ${macAddress}:`, err);
            });

        } catch (error) {
            console.error("Error processing connection:", error);
            ws.close();
        }
    });
});

// Function to save JSON to MongoDB
async function saveJsonToDatabase(jsonData) {
    try {
        const newData = new DataModel(jsonData);
        await newData.save();
        console.log("Data saved to MongoDB:", jsonData);
    } catch (error) {
        console.error("Error saving data to MongoDB:", error);
    }
}
