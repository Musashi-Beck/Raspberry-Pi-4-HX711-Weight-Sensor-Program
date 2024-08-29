const fs = require('fs');
const mysql = require('mysql2/promise');
const express = require('express');
const http = require('http');
const socketIo = require('socket.io');
const path = require('path');
const Gpio = require('onoff').Gpio;

// 設置Express應用和Socket.IO
const app = express();
const server = http.createServer(app);
const io = socketIo(server);

// RGB LED 設置
const redPin = new Gpio(16, 'out');
const greenPin = new Gpio(20, 'out');
const bluePin = new Gpio(21, 'out');

// LED 控制函數
function setLED(r, g, b) {
  redPin.writeSync(r);
  greenPin.writeSync(g);
  bluePin.writeSync(b);
}

// LED 顏色函數
function setPink() { setLED(1, 0, 1); }
function setWhite() { setLED(1, 1, 1); }
function setGreen() { setLED(0, 1, 0); }
function setRed() { setLED(1, 0, 0); }
function turnOffLED() { setLED(0, 0, 0); }

// MySQL 連接池設置
const pool = mysql.createPool({
  host: '192.168.40.119',
  user: 'rootuser',
  password: '1234',
  port: '3306',
  database: 'WineStorage',
  waitForConnections: true,
  connectionLimit: 10,
  queueLimit: 0
});

// 設置靜態文件服務
app.use(express.static(path.join(__dirname, 'public')));

// WebSocket 連接處理
io.on('connection', async (socket) => {
  console.log('新的客戶端連接');
    setWhite();    // 新連接時亮白燈
    setTimeout(turnOffLED, 3000); // 3秒後關閉 LED

  try {
    const recentData = await getRecentWeightData();
    socket.emit('initialWeightData', recentData);
  } catch (error) {
    console.error('獲取初始重量數據時發生錯誤:', error);
  }

  socket.on('disconnect', () => {
    console.log('客戶端斷開連接');
    setRed();    // 斷開連接時亮紅燈
    setTimeout(turnOffLED, 3000); // 3秒後關閉 LED
  });
});

// 從 MySQL 讀取最近的重量數據
async function getRecentWeightData() {
  const connection = await pool.getConnection();
  try {
    const [rows] = await connection.query(
      'SELECT sensor1_weight, sensor2_weight, timestamp FROM weight_logs ORDER BY timestamp DESC LIMIT 10'
    );
    return rows;
  } finally {
    connection.release();
  }
}

// 讀取重量
async function readWeight(file) {
  return new Promise((resolve, reject) => {
    fs.readFile(file, 'utf8', (err, data) => {
      if (err) reject(err);
      else resolve(Math.max(0, parseFloat(data.trim())));    // 確保重量不為負
    });
  });
}

// 儲存最近的重量數據和檢查穩定性
const recentWeights = [[], []];
let lastStoredWeights = [null, null];

// 檢查重量是否穩定
function isWeightStable(weights) {
  if (weights.length < 5) return false;
  const maxDiff = Math.max(...weights) - Math.min(...weights);
  return maxDiff <= 10;    // 如果最大差異不超過10g，則認為穩定
}

async function logWeights() {
  try {
    // 同時讀取兩個重量感測器的數據
    const weights = await Promise.all([
      readWeight('/proc/weight1'),
      readWeight('/proc/weight2')
    ]);

    let weightChanged = false;
    weights.forEach((weight, index) => {
      // 檢查重量是否有顯著變化（超過10g），使用 Math.abs()取絕對值確保重量不論增減都能檢測到變化
      if (recentWeights[index].length > 0 && Math.abs(weight - recentWeights[index][recentWeights[index].length - 1]) > 10) {
        weightChanged = true;
      }
      // 將新的重量添加到最近重量列表中
      recentWeights[index].push(weight);
      // 如果列表長度超過5，移除最舊的重量
      if (recentWeights[index].length > 5) recentWeights[index].shift();
    });

    // 如果重量發生顯著變化，點亮粉燈
    if (weightChanged) {
      setPink();
      setTimeout(turnOffLED, 5000);
    }

    // 判斷是否應該存儲新的重量數據
    let shouldStore = isWeightStable(recentWeights[0]) && isWeightStable(recentWeights[1]) &&
      (lastStoredWeights[0] === null || lastStoredWeights[1] === null ||
      Math.abs(weights[0] - lastStoredWeights[0]) > 10 ||
      Math.abs(weights[1] - lastStoredWeights[1]) > 10);

    if (shouldStore) {
      const connection = await pool.getConnection();
      try {
        // 將新的重量數據插入數據庫
        await connection.query(
          'INSERT INTO weight_logs (sensor1_weight, sensor2_weight) VALUES (?, ?)',
          weights
        );
        console.log(`已記錄重量: 感測器1 = ${weights[0]}g, 感測器2 = ${weights[1]}g`);
        // 更新最後存儲的重量
        lastStoredWeights = weights;

        // 點亮綠色LED表示存儲成功
        setGreen();
        setTimeout(turnOffLED, 3000);
      } finally {
        connection.release();    // 釋放數據庫連接
      }
    }

    // 通過WebSocket向所有連接的客戶端發送重量更新
    io.emit('weightUpdate', { weight1: weights[0], weight2: weights[1] });
  } catch (error) {
    console.error('記錄重量時發生錯誤:', error);
  }
}

// 主程序
async function main() {
  try {
    await pool.getConnection(); // 檢查資料庫連接
    console.log('成功連接到 MySQL 數據庫，開始記錄重量數據...');

    // 初始化 LED
    turnOffLED();

    // 每秒記錄一次重量
    setInterval(logWeights, 1000);

    // 每1秒從數據庫獲取最新數據並廣播
    setInterval(async () => {
      try {
        const recentData = await getRecentWeightData();
        io.emit('recentWeightData', recentData);
      } catch (error) {
        console.error('獲取最新重量數據時發生錯誤:', error);
      }
    }, 1000);

    // 啟動 Web 服務器
    const PORT = 3000;
    server.listen(PORT, '192.168.137.10', () => {
      console.log(`伺服器運行在 http://192.168.137.10:${PORT}`);
    });
  } catch (error) {
    console.error('無法連接到 MySQL 數據庫:', error.message);
    process.exit(1);
  }
}

// 程序結束時清理 GPIO
process.on('SIGINT', () => {
  redPin.unexport();
  greenPin.unexport();
  bluePin.unexport();
  process.exit();
});

// 啟動主程序
main();