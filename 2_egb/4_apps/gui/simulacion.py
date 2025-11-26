import random
import time
from flask import Flask, render_template, jsonify, request

app = Flask(__name__)

print("--- El script ha comenzado a leerse ---")

# --- MEMORIA SIMULADA ---
estado_simulado = {
    'spo': 0.0,    # Setpoint
    'tip': 0,      # Tipo (0=Escalon, 1=Rampa)
    'pen': 10.0,   # Pendiente
    'bde': 5.0,    # Banda de error
    'ang': 0.0,    # Ángulo actual
    'pwm': 0.0     # PWM actual
}

@app.route('/')
def index():
    return render_template('index.html')

# API individual (la de los botones pequeños)
@app.route('/api/set', methods=['POST'])
def set_config():
    data = request.json
    cmd = data.get('cmd')
    val = float(data.get('val', 0))
    
    if cmd in estado_simulado:
        estado_simulado[cmd] = val
        print(f"--> [INDIVIDUAL] {cmd} = {val}")
        return jsonify({'status': 'ok', 'device_response': 'Set ok'})
    
    return jsonify({'status': 'error', 'device_response': 'Error cmd'}), 400

# --- NUEVO: API PARA ENVIAR TODO JUNTO ---
@app.route('/api/set_all', methods=['POST'])
def set_all_config():
    data = request.json
    # data es un diccionario: {'spo': 90, 'tip': 1, 'pen': 50, 'bde': 2}
    
    print("--> [MASIVO] Recibiendo configuración completa...")
    
    # Actualizamos todo en la memoria simulada
    # (En la Raspberry real, aquí enviaríamos 4 comandos UART seguidos)
    for key, val in data.items():
        if key in estado_simulado:
            estado_simulado[key] = float(val)
            print(f"    Seteando {key} -> {val}")

    return jsonify({'status': 'ok', 'device_response': 'Configuración Completa Recibida'})
# ------------------------------------------

@app.route('/api/data')
def get_data():
    error_real = estado_simulado['spo'] - estado_simulado['ang']
    
    velocidad = error_real * 0.1 
    if velocidad > 5: velocidad = 5
    if velocidad < -5: velocidad = -5
    
    estado_simulado['ang'] += velocidad
    ruido = random.uniform(-0.5, 0.5)
    
    estado_simulado['pwm'] = error_real * 10
    if estado_simulado['pwm'] > 2000: estado_simulado['pwm'] = 2000
    if estado_simulado['pwm'] < -2000: estado_simulado['pwm'] = -2000

    return jsonify({
        'error': round(error_real + ruido, 2),
        'pwm':   round(abs(estado_simulado['pwm']), 0),
        'spo':   estado_simulado['spo'],
        'tip':   int(estado_simulado['tip']),
        'pen':   estado_simulado['pen'],
        'ang':   round(estado_simulado['ang'] + ruido, 2)
    })

if __name__ == '__main__':
    print("----------------------------------------------------")
    print(" INICIANDO SIMULADOR")
    print(" Abre tu navegador en: http://127.0.0.1:5000")
    print("----------------------------------------------------")
    app.run(debug=True, port=5000)