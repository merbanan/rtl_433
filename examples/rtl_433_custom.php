#!/usr/bin/php
<?php
/*
Short example of an UDP server written in PHP for rtl_433.

You need to have `php_cli` installed.

To run this as service use these systemd service files for
ubuntu, lubuntu, xubuntu, debian, ubuntu core and so on.

## Service file to start the rtl_433 process


[Unit]
Description=rtl433 udp service
After=network.target
[Service]
Restart=always
RestartSec=5
RemainAfterExit=no
User=root
ExecStart=/bin/sh -c "/usr/bin/rtl_433 -f 433920000 -f 433920000 -H 120 -F syslog:127.0.0.1:1433"

[Install]
WantedBy=multi-user.target


## Service file to start the php udp server


[Unit]
Description=syslog 433 udp service
After=network.target
StartLimitIntervalSec=0
[Service]
Type=simple
Restart=always
RestartSec=1
User=root
ExecStart=/usr/bin/php /home/user/rtl_433/examples/rtl_433_custom.php "0"

[Install]
WantedBy=multi-user.target
*/

error_reporting(E_ALL | E_STRICT);

//switch on debug mode
$debug = "1";
if (sizeof($argv) > 1)
{
    $debug  = $argv[1];
}
//udp server IP and Port for listen
$UDP_IP = "127.0.0.1";
$UDP_PORT = 1433;
//create socket and bind them
$socket = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
socket_bind($socket, $UDP_IP, $UDP_PORT);
//init of this variables
$from = '';
$port = 0;

//use the output of rtl_433 -f 433920000 -f 433920000 -H 120 -F syslog:127.0.0.1:1433"
//returns the json payload
function parse_syslog($line)
{
    //Try to extract the payload from a syslog line.//
    $line = mb_convert_encoding($line, "ASCII");

    if (startsWith($line,"<"))
    {
        //fields should be "VER", timestamp, hostname, command, pid, mid, sdata, payload
        $fields = explode(" ",$line, 8);
        $line = $fields[7];
    }
    return $line;
}

//server main loop
for (;;)
{
    //read from $socket into $line
    socket_recvfrom($socket, $line, 1024, 0, $from, $port);
    try
    {
        //parse $line -> returns the json payload
        $line = parse_syslog($line);

        /*
        do something with content of $line
        for example decode $line into a array
        $arr = json_decode($line,true);

        do something with that array and
        puted into a file as json

        file_put_contents('test.json', json_encode($arr, JSON_PRETTY_PRINT);
        */
    }
    catch (Exception $e) {
        echo "---------------------------------------------\n";
        echo 'Exception intercepted: ', $e->getMessage(), "\n";
        echo "------------------------------------------- -\n";
    }
}
?>

