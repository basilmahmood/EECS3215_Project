const SerialPort = require('serialport');
const express = require('express');
const app = express();
var path = require('path');
const Delimiter = require('@serialport/parser-delimiter')

const port = new SerialPort('COM3', {
  baudRate: 300,
});
const parser = port.pipe(new Delimiter({ delimiter: '\n' }))

let temp = 0;
let tempHistory = [];
let logs = [];

parser.on('data', (buffer) => {
  const buffStr = buffer.toString();
  console.log(buffStr);

  // Temp Logic
  if (buffStr.charAt(0) == 'T') {
    let tempString = buffStr.substring(1);
    temp = parseFloat(tempString)/(Math.pow(10, 3 + tempString.length - 5)); // Ensures 2 numbers after the decimal point incase of error
    tempHistory.push({
      temp,
      timeStamp: Date.now(),
    });
    if (tempHistory.length > 100) tempHistory.shift(); // Control array size
  }

  // Button Logic
  else if (buffStr.charAt(0) == 'B') {
    const button = buffStr.substring(1);
    let logTime = {
      '1': 0, // Button 1 is instantaneous
      '2': 5, // Button 2 records 5 sec
      '3': 60 // Button 3 records 60
    };
    const endInterval = tempHistory[tempHistory.length - 1].timeStamp;
    let startInterval = tempHistory[tempHistory.length - 1].timeStamp;

    let tempAvg = tempHistory[tempHistory.length - 1].temp;

    for (let i = tempHistory.length - 2; i >= 0; i--) {
      if ((endInterval - startInterval) >= (logTime[button] * 1000)) break;
      tempAvg += tempHistory[i].temp;
      startInterval = tempHistory[i].timeStamp;
    }

    let interval;
    if (button == '1') {
      interval = "Instantaneous Reading"
    } else {
      const endDate = new Date(endInterval);
      const startDate = new Date(startInterval);
      interval = `${startDate.toTimeString().split(" ")[0]} - ${endDate.toTimeString().split(" ")[0]}`;
    }
    logs.push({
      timeStamp: Date.now(), // Button pressed
      tempAvg,
      interval
    });
  }
  
}) 

app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname + '/index.html'));
});

app.get('/temp', (req, res) => {
  res.send(temp.toString());
});

app.get('/logs', (req, res) => {
  res.send(logs);
});
 
app.listen(3000);