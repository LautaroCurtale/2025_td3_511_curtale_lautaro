import os
import time
from flask import Flask, render_template, jsonify, request

app = Flask(__name__)

# Ruta al dispositivo del kernel
DEV_PATH = "/dev/egb"

def enviar_uart(cmd):
    """Envía un comando al driver y espera respuesta"""
    try:
        # Escribir comando
        with open(DEV_PATH, "w") as dev:
            dev.write(cmd + "\n")
        
        # Leer respuesta (bloqueante hasta que el Pico responda)
        with open(DEV_PATH, "r") as dev:
            resp = dev.read().strip()
        return resp
    except Exception as e:
        print(f"Error UART: {e}")
        return "Error"

def parse_val(resp):
    """Convierte 'clave=valor' a float de forma segura"""
    try:
        if "=" in resp:
            return float(resp.split('=')[1])
        return 0.0
    except:
        return 0.0

@app.route('/')
def index():
    return render_template('index.html')

# API para enviar configuración individual
@app.route('/api/set', methods=['POST'])
def set_config():
    data = request.json
    mensaje = f"set {data['cmd']} {data['val']}"
    respuesta = enviar_uart(mensaje)
    return jsonify({'status': 'ok', 'device_response': respuesta})

# --- NUEVO: API PARA ENVIAR TODO JUNTO ---
@app.route('/api/set_all', methods=['POST'])
def set_all_config():
    data = request.json
    # Recibimos: {'spo': XX, 'tip': X, 'pen': XX, 'bde': XX}
    
    try:
        # Enviamos los comandos uno por uno al UART
        # Es importante el orden: Setpoint al final para que el movimiento inicie
        # con los parametros de velocidad ya configurados.
        
        # 1. Tipo de entrada
        enviar_uart(f"set tip {data['tip']}")
        # 2. Pendiente
        enviar_uart(f"set pen {data['pen']}")
        # 3. Banda de Error
        enviar_uart(f"set bde {data['bde']}")
        # 4. Setpoint (Dispara el movimiento)
        resp_final = enviar_uart(f"set spo {data['spo']}")
        
        return jsonify({'status': 'ok', 'device_response': 'Configurada'})
    except Exception as e:
        return jsonify({'status': 'error', 'device_response': str(e)})

# API para leer datos en tiempo real
@app.route('/api/data')
def get_data():
    # 1. Pedir Error
    raw_err = enviar_uart("get err")
    # 2. Pedir PWM
    raw_pwm = enviar_uart("get pwm")
    # 3. Pedir Setpoint (Leido del micro)
    raw_spo = enviar_uart("get spo")
    # 4. Pedir Tipo Entrada (Leido del micro)
    raw_tip = enviar_uart("get tip")
    # 5. Pedir Pendiente (Opcional, si el firmware lo soporta)
    # raw_pen = enviar_uart("get pen") 

    # Nota: get pen no lo agregamos al firmware abajo para ahorrar espacio,
    # pero el HTML lo muestra. Si quieres leerlo del micro, avísame.
    # Por ahora el HTML mostrará 0 o lo que le mandes.

    return jsonify({
        'error': parse_val(raw_err),
        'pwm':   parse_val(raw_pwm),
        'spo':   parse_val(raw_spo),
        'tip':   parse_val(raw_tip),
        'pen':   0.0, # Placeholder si no leemos pen del micro
        'ang':   0.0  # El angulo lo calculamos en el front o pedimos 'get ang'
    })

# Agregamos una ruta extra para pedir el angulo si lo quieres mostrar
@app.route('/api/data_full')
def get_data_full():
    # Esta ruta pide TODO, incluyendo angulo
    raw_err = enviar_uart("get err")
    raw_ang = enviar_uart("get ang") # <--- Pide angulo
    raw_pwm = enviar_uart("get pwm")
    raw_spo = enviar_uart("get spo")
    raw_tip = enviar_uart("get tip")

    return jsonify({
        'error': parse_val(raw_err),
        'ang':   parse_val(raw_ang),
        'pwm':   parse_val(raw_pwm),
        'spo':   parse_val(raw_spo),
        'tip':   parse_val(raw_tip),
        'pen':   0.0 
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)