from flask import Flask, request, jsonify, render_template_string
import socket
import hashlib

app = Flask(__name__)

# --- C++ DATABASE CONFIG ---
DB_HOST = '127.0.0.1'
DB_PORT = 8081

def send_to_db(command):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((DB_HOST, DB_PORT))
        s.sendall(command.encode())
        response = s.recv(1024).decode().strip()
        s.close()
        return response
    except Exception as e:
        return f"DB_ERROR: {e}"

def hash_password(password):
    return hashlib.sha256(password.encode()).hexdigest()

# --- API ROUTES ---

@app.route('/register', methods=['POST'])
def register():
    data = request.json
    username = data.get('username')
    password = data.get('password')

    if send_to_db(f"GET user_{username}\n") != "(nil)":
        return jsonify({"status": "error", "message": "User already exists!"}), 400

    if send_to_db(f"SET user_{username} {hash_password(password)}\n") == "OK":
        return jsonify({"status": "success", "message": "Registration successful! You can now login."})
    return jsonify({"status": "error", "message": "Database error."}), 500

@app.route('/login', methods=['POST'])
def login():
    data = request.json
    username = data.get('username')
    password = data.get('password')

    stored_hash = send_to_db(f"GET user_{username}\n")
    
    if stored_hash == "(nil)":
        return jsonify({"status": "error", "message": "User not found."}), 404

    if stored_hash == hash_password(password):
        return jsonify({"status": "success", "message": "Success!"})
    else:
        return jsonify({"status": "error", "message": "Incorrect password."}), 401


# --- THE NEW PROTECTED SITE ---
@app.route('/dashboard')
def dashboard():
    # Grab the username from the URL bar to personalize it
    user = request.args.get('user', 'Guest')
    
    return f"""
    <body style="background: #121212; color: #2ea043; font-family: Arial; text-align: center; margin-top: 15%;">
        <h1>🚀 Authentication Successful!</h1>
        <h2>Welcome to Pavan M's Secure Dashboard, {user}.</h2>
        <p style="color: #888;">If you are seeing this page, your C++ database verified your SHA-256 hash perfectly.</p>
        <br>
        <a href="/" style="color: #0078D4; text-decoration: none; font-weight: bold;">Log out and return home</a>
    </body>
    """

# --- FRONTEND (HTML/JS) ---
HTML_PAGE = """
<!DOCTYPE html>
<html>
<head>
    <title>C++ DB Login</title>
    <style>
        body { font-family: Arial; display: flex; justify-content: center; margin-top: 100px; background: #121212; color: white;}
        .card { background: #1e1e1e; padding: 30px; border-radius: 10px; width: 300px; box-shadow: 0 4px 8px rgba(0,0,0,0.5); }
        input { width: 90%; padding: 10px; margin: 10px 0; border-radius: 5px; border: none; }
        button { width: 100%; padding: 10px; margin-top: 10px; background: #0078D4; color: white; border: none; border-radius: 5px; cursor: pointer; font-weight: bold;}
        button:hover { background: #005A9E; }
        #msg { margin-top: 15px; text-align: center; font-size: 14px; }
    </style>
</head>
<body>
    <div class="card">
        <h2>Auth Portal</h2>
        <input type="text" id="user" placeholder="Username" required>
        <input type="password" id="pass" placeholder="Password" required>
        <button onclick="auth('login')">Login</button>
        <button onclick="auth('register')" style="background: #2ea043;">Register</button>
        <div id="msg"></div>
    </div>

    <script>
        async function auth(action) {
            const u = document.getElementById('user').value;
            const p = document.getElementById('pass').value;
            const msgBox = document.getElementById('msg');
            
            if(!u || !p) { msgBox.innerHTML = "Fill all fields!"; msgBox.style.color = "red"; return; }

            const res = await fetch('/' + action, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ username: u, password: p })
            });
            const data = await res.json();
            
            // --- THE NEW REDIRECT LOGIC ---
            if (data.status === "success" && action === 'login') {
                // If login works, physically redirect the browser to the new site
                window.location.href = '/dashboard?user=' + encodeURIComponent(u);
            } else {
                // Otherwise, just show the error or success message on the same screen
                msgBox.innerHTML = data.message;
                msgBox.style.color = data.status === "success" ? "#2ea043" : "#f85149";
            }
        }
    </script>
</body>
</html>
"""

@app.route('/')
def home():
    return render_template_string(HTML_PAGE)

if __name__ == '__main__':
    print("🚀 Web server starting on http://127.0.0.1:5000")
    app.run(port=5000)