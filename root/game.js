// 获取Canvas元素和2D上下文
var canvas = document.getElementById("gameCanvas");
var context = canvas.getContext("2d");
var times=0;
var gameState = 1;	// 游戏状态，1表示游戏进行中，0表示游戏结束
var winner="";

// 保存棋盘状态的二维数组，初始为全空
var boardState = [
  ["", "", ""],  // 第一行
  ["", "", ""],  // 第二行
  ["", "", ""]   // 第三行
];

// 绘制棋盘
function drawBoard() {
	// 设置线条宽度
  context.lineWidth = 3;
  
    // 绘制画布边框
  context.strokeRect(0, 0, canvas.width, canvas.height);
  
  context.beginPath();
  // 画水平线
  context.moveTo(canvas.width , canvas.height * 0.333);
  context.lineTo(canvas.width * 0, canvas.height * 0.333);
  context.moveTo(canvas.width , canvas.height * 0.667);
  context.lineTo(canvas.width * 0, canvas.height * 0.667);
  // 画垂直线
  context.moveTo(canvas.width * 0.333, canvas.height );
  context.lineTo(canvas.width * 0.333, canvas.height * 0);
  context.moveTo(canvas.width * 0.667, canvas.height );
  context.lineTo(canvas.width * 0.667, canvas.height * 0);
  context.stroke();
  
}

// 判断是否胜利
function checkWin(piece) {
  // 检查行
  for (var row = 0; row < 3; row++) {
    if (boardState[row][0] === piece &&boardState[row][1] === piece && boardState[row][2] === piece) 
	{
      return true;
    }
  }

  // 检查列
  for (var col = 0; col < 3; col++) {
    if (boardState[0][col] === piece && boardState[1][col] === piece && boardState[2][col] === piece) {
      return true;
    }
  }

  // 检查对角线
  if (boardState[0][0] === piece &&boardState[1][1] === piece &&boardState[2][2] === piece) 
  {
    return true;
  }
  if (boardState[0][2] === piece && boardState[1][1] === piece && boardState[2][0] === piece)
  {
    return true;
  }

  return false;
}

// 绘制棋子
function drawPiece(row, col, piece) {
    var x = col * (canvas.width / 3) + canvas.width / 6;
    var y = row * (canvas.height / 3) + canvas.height / 6;

    context.font = "64px Arial";
    context.textAlign = "center";
    context.textBaseline = "middle";

    if (piece === "X") {
        context.fillStyle = "#FF0000";
        context.fillText("X", x, y);
		boardState[row][col]="X";
		if(checkWin("X")){
			gameState=0;
			winner="X";
			context.font = "32px Arial";
			context.fillStyle = "#000000";
			context.fillText("游戏结束，获胜者："+winner, canvas.width / 2, canvas.height / 2);
		}
    } else if (piece === "O") {
        context.fillStyle = "#0000FF";
        context.fillText("O", x, y);
		boardState[row][col]="O";
		if(checkWin("O")){
			gameState=0;
			winner="O";
			context.font = "32px Arial";
			context.fillStyle = "#000000";
			context.fillText("游戏结束，获胜者："+winner, canvas.width / 2, canvas.height / 2);
		}
    }
}

// 处理点击事件
function handleMouseClick(event) {
	if(gameState===0){
		return;
	}
		
    var rect = canvas.getBoundingClientRect();
    var x = event.clientX - rect.left;
    var y = event.clientY - rect.top;

    var col = Math.floor(x / (canvas.width / 3));
    var row = Math.floor(y / (canvas.height / 3));
	
    if (boardState[row][col] === "") {
		if (times % 2 === 0) {
		  drawPiece(row, col, "O");
		  console.log("0"+times);
		} else {
		  drawPiece(row, col, "X");
		  console.log("X"+times);
		}
		times++;
		
    }
}

// 绑定点击事件处理函数
canvas.addEventListener("click", handleMouseClick);

// 绘制初始棋盘
drawBoard();