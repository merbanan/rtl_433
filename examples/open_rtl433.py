#!/usr/bin/env python3

"""Helper command for rtl_433 to visualize a sample file in a web browser."""

from http.server import BaseHTTPRequestHandler, HTTPServer
import sys
import subprocess
import webbrowser

hostName = "localhost"
serverPort = 8080


def parseToPulseData(filename):
    ret = subprocess.run(["rtl_433", "-F", "null", "-w", "OOK:-", filename], capture_output=True)
    return ret.stdout


class PulseServer(BaseHTTPRequestHandler):
    def do_GET(self):
        global pulsedata

        self.send_response(200)
        self.send_header("Content-type", "text/html")
        self.end_headers()

        self.wfile.write(bytes("""
<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta http-equiv="X-UA-Compatible" content="IE=edge">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <meta name="mobile-web-app-capable" content="yes">
    <meta name="apple-mobile-web-app-capable" content="yes">
    <link rel="apple-touch-icon" href="https://triq.org/pdv/icon.png" />
    <link rel="apple-touch-icon" sizes="76x76" href="https://triq.org/pdv/icon.76.png" />
    <link rel="apple-touch-icon" sizes="120x120" href="https://triq.org/pdv/icon.120.png" />
    <link rel="apple-touch-icon" sizes="152x152" href="https://triq.org/pdv/icon.152.png" />
    <link rel="apple-touch-icon" sizes="180x180" href="https://triq.org/pdv/icon.180.png" />
    <link rel="icon" sizes="192x192" href="https://triq.org/pdv/icon.192.png">
    <link rel="icon" sizes="128x128" href="https://triq.org/pdv/icon.128.png">
    <link rel="icon" href="https://triq.org/pdv/favicon.ico">
    <meta name="description" content="I/Q Spectrogram and Pulsedata.">
    <title>I/Q Spectrogram &amp; Pulsedata</title>
    <script defer="defer" src="https://triq.org/pdv/js/chunk-vendors.js"></script>
    <script defer="defer" src="https://triq.org/pdv/js/app.js"></script>
    <link href="https://triq.org/pdv/css/chunk-vendors.css" rel="stylesheet">
    <link href="https://triq.org/pdv/css/app.css" rel="stylesheet">
</head>
<body>
    <noscript><strong>We're sorry but I/Q Spectrogram &amp; Pulsedata doesn't work properly without JavaScript
      enabled. Please enable it to continue.</strong></noscript>
    <div id="app"></div>
<script>
window.pulseData=`
""", "utf-8"))
        self.wfile.write(pulsedata)
        self.wfile.write(bytes("""
`
</script>
</body>
</html>
""", "utf-8"))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage:\n%s FILENAME.cu8" % (sys.argv[0]))
        exit(1)

    filename = sys.argv[1]
    pulsedata = parseToPulseData(filename)

    webServer = HTTPServer((hostName, serverPort), PulseServer)
    print("If the browser doesn't open go to http://%s:%s" % (hostName, serverPort))

    try:
        webbrowser.open("http://%s:%s/" % (hostName, serverPort))
        webServer.handle_request()  # once
    except KeyboardInterrupt:
        pass

    webServer.server_close()
    print("done.")
