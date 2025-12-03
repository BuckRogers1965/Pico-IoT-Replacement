"""PicoW to Home Assistant MQTT Discovery Bridge.

This script acts as a persistent gateway between PicoW devices running the
PicoW-IoT-Framework and a Home Assistant instance. It provides true zero-touch,
automatic discovery and integration of multiple PicoW devices.
... (Full docstring) ...
"""
import requests
import paho.mqtt.client as mqtt
import json
import time
import threading
from zeroconf import ServiceBrowser, Zeroconf, ServiceListener

# --- Configuration ---
MQTT_BROKER_IP = "192.168.1.110" # IP of your Home Assistant / MQTT Broker
# --- End Configuration ---

managed_devices = {}
mqtt_client = None
COMPONENT_MAP = { 0: {"component":"sensor"}, 1: {"component":"sensor"}, 2: {"component":"number"}, 3: {"component":"switch", "payload_on":"1", "payload_off":"0"} }

class PicoDeviceManager(threading.Thread):
    def __init__(self, ip, port, name):
        super().__init__()
        self.ip, self.port, self.name = ip, port, name
        self.api_base = f"http://{self.ip}:{self.port}/api"
        self.identity = self.manifest = self.device_id = self.device_name = None
        self.stop_event = threading.Event()
        self.daemon = True

    def setup(self):
        try:
            print(f"[{self.name}] Fetching identity from {self.api_base}/identity")
            self.identity = requests.get(f"{self.api_base}/identity", timeout=5).json()
            print(f"[{self.name}] Fetching manifest...")
            self.manifest = requests.get(f"{self.api_base}/manifest", timeout=5).json()
            self.device_id, self.device_name = self.identity['device_id'], self.identity['device_name']
            self.register_with_home_assistant()
            return True
        except requests.RequestException as e:
            print(f"[{self.name}] ERROR: Could not set up device. Will retry. Error: {e}")
            return False

    def register_with_home_assistant(self):
        print(f"[{self.name}] Registering device '{self.device_name}' with Home Assistant...")
        for item in self.manifest:
            if item['type'] not in COMPONENT_MAP: continue
            info = COMPONENT_MAP[item['type']]
            comp, obj_id = info['component'], item['id']
            cfg_topic = f"homeassistant/{comp}/{self.device_id}/{obj_id}/config"
            payload = {
                "name": f"{self.device_name} {item['name']}", "unique_id": f"{self.device_id}_{obj_id}",
                "device": { "identifiers": [self.device_id], "name": self.device_name, "manufacturer": "PicoW Framework" },
                "state_topic": f"{self.device_id}/{obj_id}/state",
            }
            if 'temp' in obj_id.lower(): payload["device_class"] = "temperature"
            if 'humidity' in obj_id.lower(): payload["device_class"] = "humidity"
            if item['unit']: payload["unit_of_measurement"] = item['unit']
            if comp in ["number", "switch"]:
                payload["command_topic"] = f"{self.device_id}/{obj_id}/set"
                mqtt_client.subscribe(payload["command_topic"])
                if comp == "number": payload.update({"min": item['min_val'], "max": item['max_val'], "step": item['step']})
                else: payload.update({"payload_on": info['payload_on'], "payload_off": info['payload_off']})
            mqtt_client.publish(cfg_topic, json.dumps(payload), retain=True)
            print(f"  - Registered {obj_id} ({comp})")

    def run(self):
        if not self.setup(): return
        print(f"[{self.name}] Starting polling loop.")
        while not self.stop_event.is_set():
            try:
                data = requests.get(f"{self.api_base}/data", timeout=5).json()
                for item_id, value in data.items():
                    mqtt_client.publish(f"{self.device_id}/{item_id}/state", str(value))
            except requests.RequestException as e: print(f"[{self.name}] WARNING: Could not poll data. Error: {e}")
            self.stop_event.wait(10)
        print(f"[{self.name}] Manager thread stopped.")

    def stop(self): self.stop_event.set()

class MyListener(ServiceListener):
    def add_service(self, zc, type, name):
        info = zc.get_service_info(type, name)
        if info and name not in managed_devices:
            ip = ".".join(map(str, info.addresses[0]))
            print(f"\nDiscovered new IoT Framework device: {name} at {ip}:{info.port}")
            manager = PicoDeviceManager(ip, info.port, name)
            managed_devices[name] = manager
            manager.start()
    def remove_service(self, zc, type, name):
        if name in managed_devices: print(f"\nDevice disappeared: {name}."); managed_devices[name].stop(); del managed_devices[name]
    def update_service(self, zc, type, name): pass

def on_mqtt_message(client, userdata, msg):
    parts = msg.topic.split('/')
    if len(parts) == 3 and parts[2] == 'set':
        dev_id, item_id, _ = parts
        for mgr in managed_devices.values():
            if hasattr(mgr, 'device_id') and mgr.device_id == dev_id:
                try:
                    print(f"Routing command to {mgr.name}: {item_id} -> {msg.payload.decode()}")
                    requests.post(f"{mgr.api_base}/update", json={"id": item_id, "value": float(msg.payload.decode())}, timeout=5)
                except Exception as e: print(f"Error dispatching MQTT command: {e}")
                break

if __name__ == '__main__':
    mqtt_client = mqtt.Client()
    mqtt_client.on_message = on_mqtt_message
    try: mqtt_client.connect(MQTT_BROKER_IP, 1883, 60); print("Connected to MQTT Broker.")
    except Exception as e: print(f"FATAL: Could not connect to MQTT Broker. Error: {e}"); exit()
    mqtt_client.loop_start()
    zeroconf, listener = Zeroconf(), MyListener()
    browser = ServiceBrowser(zeroconf, "_iot-framework._tcp.local.", listener)
    print("Started mDNS browser. Listening for PicoW IoT devices...")
    try:
        while True: time.sleep(1)
    except KeyboardInterrupt: print("\nShutting down...")
    finally:
        for mgr in managed_devices.values(): mgr.stop()
        zeroconf.close(); mqtt_client.loop_stop(); print("Shutdown complete.")