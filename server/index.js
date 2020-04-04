const SerialPort = require('serialport');
const express = require('express');
const app = express();
var path = require('path');
const Delimiter = require('@serialport/parser-delimiter')

const port = new SerialPort('COM3', {
  baudRate: 300,
});
const parser = port.pipe(new Delimiter({ delimiter: '\n' }))

let temp;

parser.on('data', (buffer) => {
  console.log(buffer.toString());
  const tempString = buffer.toString();
  temp = parseFloat(tempString)/(Math.pow(10, 3 + tempString.length - 5)); // Ensures 2 numbers after the decimal point incase of error
}) 

app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname + '/index.html'));
});

app.get('/temp', (req, res) => {
  res.send(temp.toString());
});
 
app.listen(3000);