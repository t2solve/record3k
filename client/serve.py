#!/usr/bin/env python3
"""
Simple HTTP server for Live Bug Tracker 3000 client
Serves the client/ directory with CORS enabled and hot reload via SSE
"""

import http.server
import socketserver
import threading
import time
from pathlib import Path
from hashlib import md5

PORT = 3000
DIRECTORY = Path(__file__).parent
WATCH_FILES = ['*.html', '*.js', '*.css']

# Track file changes
file_hashes = {}
clients = []

def get_file_hash(filepath):
    """Calculate MD5 hash of file content"""
    try:
        return md5(filepath.read_bytes()).hexdigest()
    except:
        return None

def watch_files():
    """Watch for file changes and notify clients"""
    global file_hashes
    
    while True:
        time.sleep(0.5)  # Check every 500ms
        changed = False
        
        for pattern in WATCH_FILES:
            for filepath in DIRECTORY.glob(pattern):
                current_hash = get_file_hash(filepath)
                if filepath not in file_hashes:
                    file_hashes[filepath] = current_hash
                elif file_hashes[filepath] != current_hash:
                    print(f"🔄 Changed: {filepath.name}")
                    file_hashes[filepath] = current_hash
                    changed = True
        
        if changed:
            # Notify all connected clients
            for client in clients[:]:
                try:
                    client.wfile.write(b"data: reload\n\n")
                    client.wfile.flush()
                except:
                    clients.remove(client)

class HotReloadHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(DIRECTORY), **kwargs)

    def end_headers(self):
        # Add CORS headers
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        # Prevent caching for development
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        super().end_headers()

    def do_GET(self):
        # SSE endpoint for hot reload
        if self.path == '/_hot_reload':
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.send_header('Cache-Control', 'no-cache')
            self.send_header('Connection', 'keep-alive')
            self.end_headers()
            
            clients.append(self)
            try:
                while True:
                    time.sleep(30)  # Keep connection alive
                    self.wfile.write(b": keepalive\n\n")
                    self.wfile.flush()
            except:
                clients.remove(self)
            return
        
        # Inject hot reload script into HTML files
        if self.path == '/' or self.path.endswith('.html'):
            super().do_GET()
            return
        
        super().do_GET()

    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()

# Inject hot reload client into HTML
original_html = (DIRECTORY / 'index.html').read_text() if (DIRECTORY / 'index.html').exists() else ""
if '/_hot_reload' not in original_html:
    hot_reload_script = """
    <script>
    // Hot reload via Server-Sent Events
    const eventSource = new EventSource('/_hot_reload');
    eventSource.onmessage = (e) => {
        if (e.data === 'reload') {
            console.log('🔄 Hot reload: File changed, reloading...');
            location.reload();
        }
    };
    eventSource.onerror = () => {
        console.log('⚠️ Hot reload connection lost');
    };
    </script>
</body>"""
    
    if '<script src="app.js"></script>' in original_html:
        updated_html = original_html.replace('</body>', hot_reload_script)
        (DIRECTORY / 'index.html').write_text(updated_html)
        print("✅ Hot reload script injected into index.html")

if __name__ == '__main__':
    # Start file watcher in background
    watcher = threading.Thread(target=watch_files, daemon=True)
    watcher.start()
    
    with socketserver.TCPServer(("", PORT), HotReloadHandler) as httpd:
        print(f"🐛 Live Bug Tracker 3000 - Development Server")
        print(f"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
        print(f"📁 Serving from: {DIRECTORY}")
        print(f"🌐 Client URL:   http://localhost:{PORT}")
        print(f"🔌 API Server:   http://127.0.0.1:8080")
        print(f"🔥 Hot Reload:   Enabled (watching {', '.join(WATCH_FILES)})")
        print(f"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
        print(f"Press Ctrl+C to stop\n")
        
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n\n👋 Server stopped")
