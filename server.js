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
    stroke: Number,  
    points: [        
        {
            x: Number,  
            y: Number   
        }
    ]
});

const DataSchema = new mongoose.Schema({
    strokes: [StrokeSchema],  
    timestamp: { type: Date, default: Date.now }  
});

const DataModel = mongoose.model("Data", DataSchema);

// Serve a basic message for browser requests
app.get("/", (req, res) => {
    res.send("WebSocket Server is Running! JSON data is stored in MongoDB.");
});

// WebSocket Server
const wss = new WebSocketServer({ server });

wss.on("connection", (ws) => {
    console.log("New client connected");

    // Handle incoming messages
    ws.on("message", (message) => {
        console.log("Received:", message.toString());

        try {
            const parsedData = JSON.parse(message);
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
        console.log("Client disconnected");
    });

    // Handle errors
    ws.on("error", (err) => {
        console.error("WebSocket error:", err);
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
