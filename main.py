import requests,time

ESP32_IP = "192.168.31.88"
ESP32_PORT = 80
BASE_URL = f"http://{ESP32_IP}:{ESP32_PORT}"

def get_rf_value():
    """Consulta o último valor RF armazenado no ESP32."""
    url = f"{BASE_URL}/rf"
    try:
        response = requests.get(url, timeout=5)
        response.raise_for_status()
        data = response.json()
        print("RF Value:", data.get("rf_value"))
    except Exception as e:
        print("Error getting RF value:", e)

def send_command(command=None, key=None, hex_value=None):
    """
    Envia um comando ao ESP32.
    
    Se hex_value for fornecido, envia esse valor para transmissão.
    Caso contrário, envia o campo 'command' (e opcionalmente 'key').
    """
    url = f"{BASE_URL}/command"
    payload = {}
    if hex_value is not None:
        payload["hex"] = hex_value
    else:
        if command is not None:
            payload["command"] = command
        if key is not None:
            payload["key"] = key
    headers = {"Content-Type": "application/json"}
    try:
        response = requests.post(url, json=payload, headers=headers, timeout=5)
        response.raise_for_status()
        data = response.json()
        print("Response:", data)
        return data
    except Exception as e:
        print("Error sending command:", e)
        return None

get_rf_value()

hex_code = "00000012C92C96592C92C92C9659".replace(" ", "")
print("Transmitting hex code:", hex_code)
send_command(hex_value=hex_code)