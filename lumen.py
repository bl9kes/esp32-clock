import network
import ntptime
import time

WIFI_SSID = "Your_SSID"
WIFI_PASSWORD = "Your_Password"

# Set your timezone offset in seconds (PST = -8h, PDT = -7h)
TZ_OFFSET = -8 * 3600

def connect_wifi(timeout_s=15):
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)

    if wlan.isconnected():
        return wlan

    print("Connecting to WiFi...")
    wlan.connect(WIFI_SSID, WIFI_PASSWORD)

    start = time.time()
    while not wlan.isconnected():
        if time.time() - start > timeout_s:
            raise RuntimeError("WiFi connect timed out")
        time.sleep_ms(200)

    print("WiFi connected:", wlan.ifconfig())
    return wlan

def sync_ntp(timeout_s=10):
    # Optional: pick a reliable host
    # ntptime.host = "pool.ntp.org"
    print("Syncing time with NTP...")
    start = time.time()
    while True:
        try:
            ntptime.settime()  # sets system time to UTC
            print("NTP sync OK")
            return
        except Exception as e:
            if time.time() - start > timeout_s:
                raise RuntimeError("NTP sync failed: {}".format(e))
            time.sleep(1)

def localtime_from_utc():
    # System time is UTC; apply offset only for display
    return time.localtime(time.time() + TZ_OFFSET)

def display_time(hh, mm, ss):
    # TODO: replace this with your LED display driver calls
    # Example: tm1637.show("{:02d}{:02d}".format(hh, mm)) etc.
    print("Time: {:02d}:{:02d}:{:02d}".format(hh, mm, ss))

def main():
    connect_wifi()
    sync_ntp()

    last_sync = time.time()
    SYNC_EVERY_S = 6 * 3600  # resync every 6 hours

    last_second = -1
    while True:
        t = localtime_from_utc()
        hh, mm, ss = t[3], t[4], t[5]

        # Update display once per second (prevents extra redraws)
        if ss != last_second:
            display_time(hh, mm, ss)
            last_second = ss

        # Periodic resync (optional but recommended)
        if time.time() - last_sync > SYNC_EVERY_S:
            try:
                sync_ntp()
                last_sync = time.time()
            except Exception as e:
                # Keep running even if resync fails
                print(e)

        time.sleep_ms(50)

main()
