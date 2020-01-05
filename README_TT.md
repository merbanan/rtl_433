sudo apt-get update
sudo apt-get upgrade
    
# Abhängigkeiten installieren
sudo apt-get install libtool libusb-1.0.0-dev librtlsdr-dev rtl-sdr cmake mosquitto-clients
    
# rtl_433 Git Repository holen
git clone https://github.com/merbanan/rtl_433.git
    
# in das rtl_433 Verzeichnis wechseln
cd rtl_433/
# Erstelle ein build Verzeichnis
mkdir build
cd build/
# Compile starten
cmake ../
# Make
make
# Install
sudo make install
# Testen, Ausgeben der Programm Parameter
rtl_433 -h
# Ausgabe der Programm Parameter