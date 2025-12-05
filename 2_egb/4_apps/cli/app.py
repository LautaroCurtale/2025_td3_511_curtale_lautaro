#!/usr/bin/env python3
# Nombre: app.py
import os, re, sys, termios, time

# MODIFICADO: Apunta al nuevo device file
DEV_PATH = "/dev/egb" 

def main():
    menu = 0
    msg = ""
    while True:
        match menu:
            # Menú
            case 0:
                match menu_principal():
                    case "1":
                        msg = "set"
                        menu = 1
                    case "2":
                        msg = "get"
                        menu = 2
                    case "3":
                        # ... (lógica de salida) ...
                        print("Saliendo...")
                        break
                    case _:
                        print("\nOpción invalida")
                        flush_stdin()
                        input("Presione tecla para continuar...")
            
            # Menú para escribir
            case 1:
                opc = menu_set()
                match opc:
                    case "1": msg += " tip" # Tipo
                    case "2": msg += " spo" # Setpoint
                    case "3": msg += " pen" # Pendiente
                    case "4": msg += " bde" # Banda de Error
                    case "5": ena +- " ena" # Habilitar PID
                    case "6": menu = 0
                    case _:
                        print("\nOpción invalida")
                        flush_stdin()
                        input("Presione tecla para continuar...")
                
                if opc in ["1", "2", "3", "4", "5"]:
                    ok = 0
                    match opc:
                        case "1": # Tipo de Entrada
                            while ok == 0:
                                print("-------------------------------------------------")
                                print("Ingrese Tipo de Entrada:")
                                flush_stdin()
                                val = input("[0=Escalon, 1=Rampa]: ").strip()
                                if val in ["0", "1"]:
                                    ok = 1
                                else:
                                    print("Formato inválido.")
                            msg = f"{msg} {val}"
                        
                        case "2": # Setpoint
                            while ok == 0:
                                print("-------------------------------------------------")
                                print("Ingrese Setpoint (Ángulo):")
                                flush_stdin()
                                try:
                                    val = float(input("[0.0 a 360.0]: "))
                                    if 0.0 <= val <= 360.0:
                                        ok = 1
                                    else:
                                        print("Valor fuera de rango.")
                                except ValueError:
                                    print("Formato inválido. Ej: 90.5")
                            val = round(val, 2)
                            msg = f"{msg} {val}"
                        
                        case "3": # Pendiente
                            while ok == 0:
                                print("-------------------------------------------------")
                                print("Ingrese Pendiente (Velocidad rampa):")
                                flush_stdin()
                                try:
                                    val = float(input("[1.0 a 100.0]: "))
                                    if 1.0 <= val <= 100.0:
                                        ok = 1
                                    else:
                                        print("Valor fuera de rango.")
                                except ValueError:
                                    print("Formato inválido. Ej: 50.0")
                            val = round(val, 1)
                            msg = f"{msg} {val}"
                        
                        case "4": # Banda de Error (LEDs)
                            while ok == 0:
                                print("-------------------------------------------------")
                                print("Ingrese Banda de Error (para LEDs):")
                                flush_stdin()
                                try:
                                    val = float(input("[0.1 a 10.0 grados]: "))
                                    if 0.1 <= val <= 10.0:
                                        ok = 1
                                    else:
                                        print("Valor fuera de rango.")
                                except ValueError:
                                    print("Formato inválido. Ej: 2.5")
                            val = round(val, 2)
                            msg = f"{msg} {val}"
                        case "5": # Habilitar PID
                            while ok == 0:
                                print("-------------------------------------------------")
                                print("Control PID:")
                                flush_stdin()
                                val = input("[0=Apagar, 1=Encender]: ").strip()
                                if val in ["0", "1"]:
                                    ok = 1
                                else:
                                    print("Valor inválido. Use 0 o 1.")
                            msg = f"{msg} {val}" # Construye "set ena 1"

                    print("-------------------------------------------------")
                    print(f"Enviado por UART: {msg}")
                    print("-------------------------------------------------")
                    rta = enviar_uart(msg)
                    print(f"Respuesta: {rta}")
                    print("-------------------------------------------------")
                    input("Presione tecla para continuar...")
                    menu = 0
            
            # Menú para leer
            case 2:
                opc = menu_get()
                match opc:
                    case "1": msg += "ang" # Ángulo (se cambio de "get ang" a solo "ang")
                    case "2": msg += "pwm" # Salida PWM
                    case "3": msg += "err" # Error
                    case "4": menu = 0
                    case _:
                        print("\nOpción invalida")
                        flush_stdin()
                        input("Presione tecla para continuar...")

                if opc in ["1", "2", "3"]:
                    # Se añade "get " al comando antes de enviarlo
                    msg_to_send = f"get {msg}"
                    print("-------------------------------------------------")
                    print(f"Enviado por UART: {msg_to_send}")
                    print("-------------------------------------------------")
                    rta = enviar_uart(msg_to_send)
                    print(f"Respuesta: {rta}")
                    print("-------------------------------------------------")
                    flush_stdin()
                    input("Presione tecla para continuar...")
                    menu = 0 # Resetea el menu
                    msg = "" # Resetea el msg

def menu_principal():
    os.system("clear")
    print("-------------------------------------------------")
    print("----------{ Aplicación UART POS_CTRL }-----------")
    print("-------------------------------------------------")
    print("Ingrese accion a realizar:")
    print("-------------------------------------------------")
    print("1> Enviar configuración (SET)")
    print("2> Obtener información (GET)")
    print("3> Salir")
    print("-------------------------------------------------")
    flush_stdin()
    return input("Opción [1-3]: ").strip()

# MODIFICADO: menu_set
def menu_set():
    print("-------------------------------------------------")
    print("Seleccione variable a configurar:")
    print("-------------------------------------------------")
    print("1> Tipo de Entrada (Escalón/Rampa)")
    print("2> Setpoint (Ángulo)")
    print("3> Pendiente (Velocidad Rampa)")
    print("4> Banda de Error (LEDs)")
    print("5> Habilitar/Deshabilitar PID")
    print("6> Volver al menu anterior")
    print("-------------------------------------------------")
    flush_stdin()
    return input("Opción [1-6]: ").strip()

# MODIFICADO: menu_get
def menu_get():
    print("-------------------------------------------------")
    print("Seleccione variable a consultar:")
    print("-------------------------------------------------")
    print("1> Ángulo actual")
    print("2> Salida PWM (PID)")
    print("3> Error actual")
    print("4> Volver al menu anterior")
    print("-------------------------------------------------")
    flush_stdin()
    return input("Opción [1-4]: ").strip()

def enviar_uart(msg):
    try:
        # 1. Escribir el mensaje
        with open(DEV_PATH, "w") as dev:
            dev.write(msg + "\n")
        
        # 2. ESPERAR UN POCO (Crucial para dar tiempo al firmware)
        time.sleep(0.1) 
        
        # 3. Leer todo lo que haya en el buffer
        with open(DEV_PATH, "r") as dev:
            contenido = dev.read().strip()
            
        # 4. FILTRAR EL ECO
        # Si la respuesta contiene el mensaje que enviamos, intentamos separarlo
        if msg in contenido:
            # Separamos por saltos de linea y buscamos algo que NO sea el mensaje
            lineas = contenido.split('\n')
            for linea in lineas:
                linea_limpia = linea.strip()
                # Si la linea tiene datos y NO es exactamente lo que mandamos
                if len(linea_limpia) > 0 and linea_limpia != msg.strip():
                    return linea_limpia
            
            # Si solo estaba el eco y nada más...
            return "Esperando..."
            
        return contenido
        
    except Exception as e:
        return f"Error de E/S: {e}"

def flush_stdin():
    if os.name == 'posix':
        termios.tcflush(sys.stdin, termios.TCIFLUSH)
    else:
        # Una alternativa simple para Windows (aunque termios no existe)
        import msvcrt
        while msvcrt.kbhit():
            msvcrt.getch()

if __name__ == "__main__":
    if os.name != 'posix':
        print("Advertencia: Este script está diseñado para Linux/macOS (usa termios).")
        # No saldrá, pero flush_stdin fallará si no se maneja
        
    if not os.path.exists(DEV_PATH):
        print(f"Error: Dispositivo {DEV_PATH} no encontrado.")
        print("Asegúrese de que el módulo del kernel ('egb') esté cargado.")
    else:
        main()