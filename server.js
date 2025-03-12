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

// Define Schema for stroke points
const PointSchema = new mongoose.Schema({
    x: Number,
    y: Number
});

// Define Schema for strokes
const StrokeSchema = new mongoose.Schema({
    stroke: Number,  // Stroke ID
    points: [PointSchema]  // Array of points
});

// Define Schema for user data
const DataSchema = new mongoose.Schema({
    name: String,  // User's name
    personal_color: String,  // User's selected color
    strokes: [StrokeSchema],  // Stroke array
    timestamp: { type: Date, default: Date.now }  // Timestamp
});

const DataModel = mongoose.model("Data", DataSchema);

// Serve a basic message for browser requests
app.get("/", (req, res) => {
    res.send("WebSocket Server is Running! JSON data is stored in MongoDB.");
});

// WebSocket Server
const wss = new WebSocketServer({ server });

wss.on("connection", (ws) => {
    console.log("New client connected.");

    // Handle incoming messages
    ws.on("message", (message) => {
        console.log("Received:", message.toString());

        try {
            const jsonData = JSON.parse(message);
            saveJsonToDatabase(jsonData); // Save to database
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

    // Handle client disconnection
    ws.on("close", () => {
        console.log("Client disconnected.");
    });

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

// ✅ API Endpoint to Fetch Only `timestamp` & `name`
app.get("/data", async (req, res) => {
    try {
        const data = await DataModel.find({}, { _id: 0, timestamp: 1, name: 1 })
            .sort({ timestamp: -1 }) // Sort by latest data first
            .limit(10); // Limit to last 10 entries

        res.json(data);
    } catch (error) {
        res.status(500).json({ error: "Failed to fetch data from MongoDB" });
    }
});

app.get("/data/:timestamp", async (req, res) => {
    try {
        const { timestamp } = req.params; // Get timestamp from URL

        // Find the entry that matches the exact timestamp and exclude `_id`, `timestamp`, and `__v`
        const data = await DataModel.findOne(
            { timestamp: new Date(timestamp) },
            { _id: 0, timestamp: 0, __v: 0, "strokes._id": 0, "strokes.points._id": 0 }
        );

        if (!data) {
            return res.status(404).json({ error: "No data found for the given timestamp" });
        }

        res.json(data);
    } catch (error) {
        res.status(500).json({ error: "Failed to fetch data from MongoDB" });
    }
});
