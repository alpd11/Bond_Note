const http = require('http');
const WebSocket = require('ws');

// 创建 HTTP 服务器
const server = http.createServer((req, res) => {
  if (req.url === '/') {
    // 返回 HTML 页面
    res.writeHead(200, { 'Content-Type': 'text/html' });
    res.end(`
      <!DOCTYPE html>
      <html lang="en">
      <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>WebSocket Server</title>
      </head>
      <body>
        <h1>WebSocket Server</h1>
        <div id="messages" style="white-space: pre-wrap;"></div>
        <script>
          const ws = new WebSocket('wss://hmltry1-1e722cd26d62.herokuapp.com');
          ws.onmessage = (event) => {
            const messages = document.getElementById('messages');
            messages.textContent += event.data + '\\n';
          };
        </script>
      </body>
      </html>
    `);
  } else {
    res.writeHead(404);
    res.end('Not Found');
  }
});

// 创建 WebSocket 服务器
const wss = new WebSocket.Server({ server });

wss.on('connection', (ws) => {
  console.error(`New client connected, total clients: ${wss.clients.size}`);

  ws.on('message', (message) => {
    console.log('Received:', message.toString());
    console.error('Received (stderr):', message.toString()); //双重检查

    // 广播消息给所有客户端
    wss.clients.forEach(client => {
      if (client.readyState === WebSocket.OPEN) {
        client.send(`Server echo: ${message}`);
      }
    });
  });

  ws.on('close', () => {
    console.log('Client disconnected');
    console.error('Client disconnected');
  });
});

// 启动服务器
const port = process.env.PORT || 3000;
server.listen(port, () => {
  console.log(`Server running on port ${port}`);
});