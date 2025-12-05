import os
import time
from flask import Flask, render_template, jsonify, request
from threading import Lock  # <--- 1. Importar Lock

app = Flask(__name__)
uart_lock = Lock()

# Ruta al dispositivo del kernel
DEV_PATH = "/dev/egb"

def enviar_uart(cmd):
    with uart_lock:
        try:
            # Escribir comando
            with open(DEV_PATH, "w") as dev:
                dev.write(cmd + "\n") 
            # Leer respuesta
            with open(DEV_PATH, "r") as dev:
                resp = dev.read().strip()
            return resp
        except Exception as e:
            print(f"Error UART: {e}")
            return "Error"


def parse_val(resp):
    """Convierte 'clave=valor' a float/int de forma segura"""
    try:
        if "=" in resp:
            value_str = resp.split('=')[1]
            # Si el valor tiene decimales (ej: 90.5 o 2.5), lo dejamos en float
            if '.' in value_str:
                return float(value_str)
            # Si no tiene decimales (ej: 282, 90, 0), lo convertimos a entero
            else:
                return int(value_str)
        return 0
    except:
        return 0

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

# API PARA ENVIAR COMPLETO
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
    # 5. Pedir Pendiente 
    raw_pen = enviar_uart("get pen") 
    # 6. Pedir Angulo 
    raw_ang = enviar_uart("get ang")

    return jsonify({
        'error': parse_val(raw_err),
        'ang':   parse_val(raw_ang),
        'pwm':   parse_val(raw_pwm), 
        'spo':   parse_val(raw_spo), 
        'tip':   parse_val(raw_tip), 
        'pen':   parse_val(raw_pen), 
    })

@app.route('/api/toggle_pid', methods=['POST'])
def toggle_pid():
    data = request.json
    # Esperamos recibir JSON: {'estado': 1} o {'estado': 0}
    estado = 1 if data.get('estado') else 0
    
    # Enviamos el comando: "set ena 1" o "set ena 0"
    cmd = f"set ena {estado}"
    
    # Usar la función enviar_uart (la que tiene el Lock)
    respuesta = enviar_uart(cmd)
    
    return jsonify({'status': 'ok', 'device_response': respuesta})


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)