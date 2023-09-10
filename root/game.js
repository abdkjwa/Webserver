// ��ȡCanvasԪ�غ�2D������
var canvas = document.getElementById("gameCanvas");
var context = canvas.getContext("2d");
var times=0;
var gameState = 1;	// ��Ϸ״̬��1��ʾ��Ϸ�����У�0��ʾ��Ϸ����
var winner="";

// ��������״̬�Ķ�ά���飬��ʼΪȫ��
var boardState = [
  ["", "", ""],  // ��һ��
  ["", "", ""],  // �ڶ���
  ["", "", ""]   // ������
];

// ��������
function drawBoard() {
	// �����������
  context.lineWidth = 3;
  
    // ���ƻ����߿�
  context.strokeRect(0, 0, canvas.width, canvas.height);
  
  context.beginPath();
  // ��ˮƽ��
  context.moveTo(canvas.width , canvas.height * 0.333);
  context.lineTo(canvas.width * 0, canvas.height * 0.333);
  context.moveTo(canvas.width , canvas.height * 0.667);
  context.lineTo(canvas.width * 0, canvas.height * 0.667);
  // ����ֱ��
  context.moveTo(canvas.width * 0.333, canvas.height );
  context.lineTo(canvas.width * 0.333, canvas.height * 0);
  context.moveTo(canvas.width * 0.667, canvas.height );
  context.lineTo(canvas.width * 0.667, canvas.height * 0);
  context.stroke();
  
}

// �ж��Ƿ�ʤ��
function checkWin(piece) {
  // �����
  for (var row = 0; row < 3; row++) {
    if (boardState[row][0] === piece &&boardState[row][1] === piece && boardState[row][2] === piece) 
	{
      return true;
    }
  }

  // �����
  for (var col = 0; col < 3; col++) {
    if (boardState[0][col] === piece && boardState[1][col] === piece && boardState[2][col] === piece) {
      return true;
    }
  }

  // ���Խ���
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

// ��������
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
			context.fillText("��Ϸ��������ʤ�ߣ�"+winner, canvas.width / 2, canvas.height / 2);
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
			context.fillText("��Ϸ��������ʤ�ߣ�"+winner, canvas.width / 2, canvas.height / 2);
		}
    }
}

// �������¼�
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

// �󶨵���¼�������
canvas.addEventListener("click", handleMouseClick);

// ���Ƴ�ʼ����
drawBoard();