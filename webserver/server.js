const express = require('express');
const http = require('http');
const WebSocketServer = require('websocket').server;
const mqtt = require('mqtt');
const path = require('path');
const app = express();
const server = http.createServer(app);

// Middleware
app.use(express.json());
app.use(express.static('public'));

// MQTT Client settings
const mqttBroker = 'mqtt://192.168.43.109:1883'; // MQTT broker address
const mqttClient = mqtt.connect(mqttBroker);

// WebSocket Server
const wsServer = new WebSocketServer({
    httpServer: server,
    autoAcceptConnections: false
});

let connections = [];
let sensorStates = {
    distance: false,
    ldr: false,
    dataRate: 5
};

// WebSocket connection handler
wsServer.on('request', function(request) {
    const connection = request.accept(null, request.origin);
    connections.push(connection);
    console.log('WebSocket connection accepted:', request.origin);

    // When connection is closed
    connection.on('close', function(reasonCode, description) {
        console.log('WebSocket connection closed:', reasonCode, description);
        connections = connections.filter(conn => conn !== connection);
    });

    // When a message is received
    connection.on('message', function(message) {
        if (message.type === 'utf8') {
            try {
                const data = JSON.parse(message.utf8Data);
                handleWebSocketMessage(data);
            } catch (error) {
                console.error('WebSocket message parse error:', error);
            }
        }
    });
});

// Process WebSocket messages
function handleWebSocketMessage(data) {
    switch (data.type) {
        case 'control':
            sensorStates[data.sensor] = data.state;
           
            // Send control message to ESP32
            const controlMessage = {
                type: 'control',
                sensor: data.sensor,
                state: data.state
            };
           
            mqttClient.publish('esp32/control', JSON.stringify(controlMessage));
            console.log(`Sensor control sent: ${data.sensor} = ${data.state}`);
            break;
           
        case 'dataRate':
            sensorStates.dataRate = data.rate;
           
            // Set data transmission rate on ESP32
            const rateMessage = {
                type: 'dataRate',
                rate: data.rate
            };
           
            mqttClient.publish('esp32/config', JSON.stringify(rateMessage));
            console.log(`Data rate set: ${data.rate} seconds`);
            break;
    }
}

// Broadcast message to all WebSocket clients
function broadcastToClients(data) {
    const message = JSON.stringify(data);
    connections.forEach(connection => {
        if (connection.connected) {
            connection.sendUTF(message);
        }
    });
}

// MQTT connection events
mqttClient.on('connect', function() {
    console.log('Connected to MQTT broker');
   
    // Listen to sensor data from ESP32
    mqttClient.subscribe('esp32/sensors', function(err) {
        if (!err) {
            console.log('Subscribed to esp32/sensors topic');
        } else {
            console.error('MQTT subscription error:', err);
        }
    });
   
    // Listen to ESP32 status messages
    mqttClient.subscribe('esp32/status', function(err) {
        if (!err) {
            console.log('Subscribed to esp32/status topic');
        } else {
            console.error('MQTT subscription error:', err);
        }
    });
});

mqttClient.on('message', function(topic, message) {
    try {
        const data = JSON.parse(message.toString());
       
        switch (topic) {
            case 'esp32/sensors':
                // Forward sensor data to web clients
                broadcastToClients({
                    type: 'sensorData',
                    distance: data.distance,
                    ldr: data.ldr,
                    timestamp: new Date().toISOString()
                });
                console.log('Sensor data received:', data);
                break;
               
            case 'esp32/status':
                console.log('ESP32 status message:', data);
                // Forward status info to web clients
                broadcastToClients({
                    type: 'status',
                    ...data
                });
                break;
        }
    } catch (error) {
        console.error('MQTT message parse error:', error);
    }
});

mqttClient.on('error', function(error) {
    console.error('MQTT connection error:', error);
});

// HTTP Routes
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

// API endpoints
app.get('/api/sensors', (req, res) => {
    res.json({
        states: sensorStates,
        timestamp: new Date().toISOString()
    });
});

app.post('/api/control', (req, res) => {
    const { sensor, state } = req.body;
   
    if (sensorStates.hasOwnProperty(sensor)) {
        sensorStates[sensor] = state;
       
        // Send control message to ESP32
        const controlMessage = {
            type: 'control',
            sensor: sensor,
            state: state
        };
       
        mqttClient.publish('esp32/control', JSON.stringify(controlMessage));
       
        res.json({
            success: true,
            sensor: sensor,
            state: state,
            timestamp: new Date().toISOString()
        });
       
        console.log(`Sensor control via API: ${sensor} = ${state}`);
    } else {
        res.status(400).json({
            success: false,
            error: 'Invalid sensor name'
        });
    }
});

app.post('/api/datarate', (req, res) => {
    const { rate } = req.body;
   
    if ([1,2,3,5,10,15,20].includes(rate)) {
        sensorStates.dataRate = rate;
       
        // Set data transmission rate on ESP32
        const rateMessage = {
            type: 'dataRate',
            rate: rate
        };
       
        mqttClient.publish('esp32/config', JSON.stringify(rateMessage));
       
        res.json({
            success: true,
            dataRate: rate,
            timestamp: new Date().toISOString()
        });
       
        console.log(`Data rate set via API: ${rate} seconds`);
    } else {
        res.status(400).json({
            success: false,
            error: 'Data rate must be 1, 2, 3, 5, 10, 15 or 20 seconds'
        });
    }
});

// Error handling
app.use((err, req, res, next) => {
    console.error(err.stack);
    res.status(500).json({
        success: false,
        error: 'Server error'
    });
});

// 404 handler
app.use((req, res) => {
    res.status(404).json({
        success: false,
        error: 'Page not found'
    });
});

// Start server
const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
    console.log(`WebSocket server ready`);
    console.log(`HTTP server: http://localhost:${PORT}`);
});

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('Shutting down server...');
    mqttClient.end();
    server.close(() => {
        console.log('Server shut down');
        process.exit(0);
    });
});
