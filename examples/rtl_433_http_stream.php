#!/usr/bin/php
<?php
/*
Short example of an TCP client written in PHP for rtl_433.

You need to have `php_cli` installed.

To run this as service use these systemd service files for
ubuntu, lubuntu, xubuntu, debian, ubuntu core and so on.

## Service file to start the rtl_433 process

[Unit]
Description=rtl433 tcp service
After=network.target
[Service]
Restart=always
RestartSec=5
RemainAfterExit=no
User=root
ExecStart=/bin/sh -c "/usr/bin/rtl_433 -f 433.92M -F http:127.0.0.1:8433"

[Install]
WantedBy=multi-user.target

## Service file to start the php udp server

[Unit]
Description=http 433 tcp service
After=network.target
StartLimitIntervalSec=0
[Service]
Type=simple
Restart=always
RestartSec=1
User=root
ExecStart=/usr/bin/php /home/user/rtl_433/examples/rtl_433_http_stream.php

[Install]
WantedBy=multi-user.target
*/


//Function to check $haystack if
//$needle in $haystack
function str__contains($haystack,$needle)
{
  return (strpos($haystack, $needle) !== false);
}


//main
$addr  = "127.0.0.1";
$port = "8433";
$url  = $addr . ":" . $port . "/stream";

$fp = stream_socket_client("tcp://" . $url , $errno, $errstr, 70);
if (!$fp) {
    //optional error output
    //echo "$errstr ($errno)<br />\n";
} else {
    fwrite($fp, "GET / HTTP/1.0\r\nHost: " . $addr . "\r\nAccept: */*\r\n\r\n");
    while (!feof($fp)) {
        $line = fgets($fp, 1024);
        //time is available in all received records, that is the filter word
        //for sensor data
        if(str__contains($line,"time"))
        {
          //raw output of json
          print_r($line);
          /*
          do something with content of $line
          for example decode $line into an array
          $arr = json_decode($line,true);

          do something with that array and
          output into a file as json
          file_put_contents('test.json', json_encode($arr, JSON_PRETTY_PRINT);
          */
        }
    }
    fclose($fp);
}
?>
