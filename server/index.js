const SerialPort = require('serialport');
const express = require('express');
const app = express();
const Delimiter = require('@serialport/parser-delimiter')

const port = new SerialPort('COM3', {
  baudRate: 300,
});
const parser = port.pipe(new Delimiter({ delimiter: '\n' }))

let temp;

parser.on('data', (buffer) => {
  temp = parseFloat(buffer.toString())/1000;
  console.log(buffer.toString());
}) 

app.get('/', (req, res) => {
  res.send(temp.toString());
});
 
app.listen(3000);