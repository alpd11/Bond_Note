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
const DataSchema = new mongoose.Schema({
    mac: String,  // MAC Address of the device
    x: Number,
    y: Number,
    pressure: Number,
    timestamp: { type: Date, default: Date.now }
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
            
            // Prevent duplicate MAC connections
            if (clients.has(macAddress)) {
                console.warn(`Device with MAC ${macAddress} is already connected.`);
                ws.send(JSON.stringify({ error: "Device already connected" }));
                ws.close();
                return;
            }

            // Register the new connection
            clients.set(macAddress, ws);
            console.log(`Device with MAC ${macAddress} connected.`);

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

            // Handle disconnection
            ws.on("close", () => {
                clients.delete(macAddress);
                console.log(`Device with MAC ${macAddress} disconnected.`);
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
