#include <M5Core2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include <PNGdec.h>
#include <ArduinoJson.h>

// Wi-Fi情報
const char* ssid = "XXXX";       // 実際のSSIDを入力
const char* password = "XXXX";     // 実際のWi-Fiパスワードを入力

// OpenAI API情報
const char* openai_api_key = "sk-proj-XXXX"; // 実際のAPIキーを入力
const char* openai_host = "api.openai.com";
const int openai_port = 443;
const char* openai_endpoint = "/v1/images/edits";

// PNGデコーダのインスタンス
PNG png;

// PNGデコーダのコールバック関数
void pngDraw(PNGDRAW *pDraw) {
    uint16_t lineBuffer[pDraw->iWidth];

    // RGB565フォーマットでデータを取得
    png.getLineAsRGB565(pDraw, lineBuffer, 0x0000, 0xFFFF);

    // エンディアン変換
    for (int i = 0; i < pDraw->iWidth; i++) {
        lineBuffer[i] = (lineBuffer[i] >> 8) | (lineBuffer[i] << 8);
    }

    int imageHeight = png.getHeight();
    int x = (320 - pDraw->iWidth) / 2;
    int y = (240 - imageHeight) / 2 + pDraw->y;

    if (y >= 0 && y < 240) {
        M5.Lcd.pushImage(x, y, pDraw->iWidth, 1, lineBuffer);
    }
}

// SDカードからPNG画像を表示する関数
void displayImageFromSD(const char* filename) {
    File file = SD.open(filename);
    if (!file) {
        Serial.println("Failed to open file");
        M5.Lcd.println("Failed to open file");
        return;
    }

    int fileSize = file.size();
    uint8_t* buffer = (uint8_t*)ps_malloc(fileSize);
    if (!buffer) {
        Serial.println("Failed to allocate memory");
        M5.Lcd.println("Memory allocation failed");
        file.close();
        return;
    }

    file.read(buffer, fileSize);
    file.close();

    int rc = png.openRAM(buffer, fileSize, pngDraw);
    if (rc == PNG_SUCCESS) {
        M5.Lcd.clear();
        int decodeResult = png.decode(NULL, 0);
        if (decodeResult != 0) {
            Serial.printf("PNG decode failed with code: %d\n", decodeResult);
            M5.Lcd.println("Decode failed");
        } else {
            Serial.println("Image displayed successfully.");
        }
        png.close();
    } else {
        Serial.printf("PNG openRAM failed with code: %d\n", rc);
        M5.Lcd.println("Failed to open PNG");
    }
    free(buffer);
}

// OpenAIに画像編集リクエストを送信する関数
void sendImageEditRequest() {
    M5.Lcd.clear();
    M5.Lcd.println("Sending request...");
    Serial.println("Preparing to send image edit request to OpenAI...");

    WiFiClientSecure client;
    client.setInsecure();  // SSL証明書の検証をスキップ

    if (!client.connect(openai_host, openai_port)) {
        Serial.println("Connection to OpenAI failed");
        M5.Lcd.println("Connection failed");
        return;
    }

    // SDカードから画像とマスクファイルを読み込む
    File imageFile = SD.open("/elephant.png", FILE_READ);
    File maskFile = SD.open("/mask.png", FILE_READ);

    if (!imageFile || !maskFile) {
        Serial.println("Failed to open image or mask file");
        M5.Lcd.println("File open failed");
        return;
    }

    // ファイルサイズを取得
    size_t imageSize = imageFile.size();
    size_t maskSize = maskFile.size();

    // multipart/form-dataの境界線を設定
    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    String contentType = "multipart/form-data; boundary=" + boundary;

    // POSTリクエストのヘッダーを準備
    String headers = "";
    headers += "POST " + String(openai_endpoint) + " HTTP/1.1\r\n";
    headers += "Host: " + String(openai_host) + "\r\n";
    headers += "Authorization: Bearer " + String(openai_api_key) + "\r\n";
    headers += "Content-Type: " + contentType + "\r\n";
    headers += "Connection: close\r\n";

    // multipart/form-dataのボディを準備
    String bodyStart = "";
    bodyStart += "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"image\"; filename=\"elephant.png\"\r\n";
    bodyStart += "Content-Type: image/png\r\n\r\n";

    String bodyMiddle = "";
    bodyMiddle += "\r\n--" + boundary + "\r\n";
    bodyMiddle += "Content-Disposition: form-data; name=\"mask\"; filename=\"mask.png\"\r\n";
    bodyMiddle += "Content-Type: image/png\r\n\r\n";

    String bodyPrompt = "";
    bodyPrompt += "\r\n--" + boundary + "\r\n";
    bodyPrompt += "Content-Disposition: form-data; name=\"prompt\"\r\n\r\n";
    bodyPrompt += "元の画像に対して、おにぎりを1つ食べて大きくなったところを想像して画像を作成して。\r\n";

    // sizeパラメータを追加
    String bodySize = "";
    bodySize += "\r\n--" + boundary + "\r\n";
    bodySize += "Content-Disposition: form-data; name=\"size\"\r\n\r\n";
    bodySize += "256x256\r\n";

    String bodyEnd = "";
    bodyEnd += "--" + boundary + "--\r\n";

    // コンテンツ長を計算
    size_t contentLength = bodyStart.length() + imageSize + 2 + bodyMiddle.length() + maskSize + 2 + bodyPrompt.length() + bodySize.length() + bodyEnd.length();

    headers += "Content-Length: " + String(contentLength) + "\r\n\r\n";

    // ヘッダーを送信
    client.print(headers);
    Serial.println("Request Headers:");
    Serial.println(headers);

    // ボディの開始部分を送信
    client.print(bodyStart);
    Serial.println("Body Start:");
    Serial.println(bodyStart);

    // 画像ファイルの内容を送信
    uint8_t buffer[1024];
    size_t bytesRead = 0;
    while (bytesRead < imageSize) {
        size_t len = imageFile.read(buffer, sizeof(buffer));
        client.write(buffer, len);
        bytesRead += len;
    }
    imageFile.close();

    // 画像データの後に\r\nを追加
    client.print("\r\n");

    // ボディの中間部分を送信
    client.print(bodyMiddle);
    Serial.println("Body Middle:");
    Serial.println(bodyMiddle);

    // マスクファイルの内容を送信
    bytesRead = 0;
    while (bytesRead < maskSize) {
        size_t len = maskFile.read(buffer, sizeof(buffer));
        client.write(buffer, len);
        bytesRead += len;
    }
    maskFile.close();

    // マスクデータの後に\r\nを追加
    client.print("\r\n");

    // プロンプトを送信
    client.print(bodyPrompt);
    Serial.println("Body Prompt:");
    Serial.println(bodyPrompt);

    // sizeパラメータを送信
    client.print(bodySize);
    Serial.println("Body Size:");
    Serial.println(bodySize);

    // ボディの終了部分を送信
    client.print(bodyEnd);
    Serial.println("Body End:");
    Serial.println(bodyEnd);

    // レスポンスを読み取る
    String responseHeaders = "";
    String responseBody = "";
    bool headersEnded = false;
    while (client.connected() || client.available()) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (!headersEnded) {
                responseHeaders += line + "\n";
                if (line == "\r") {
                    headersEnded = true;
                }
            } else {
                responseBody += line + "\n";
            }
        }
    }
    client.stop();

    // レスポンスヘッダーとボディを表示
    Serial.println("Response Headers:");
    Serial.println(responseHeaders);
    Serial.println("Response Body:");
    Serial.println(responseBody);

    // JSONレスポンスを解析して画像URLを取得
    DynamicJsonDocument jsonDoc(8192);
    DeserializationError error = deserializeJson(jsonDoc, responseBody);
    if (error) {
        Serial.print("JSON deserialization failed: ");
        Serial.println(error.c_str());
        M5.Lcd.println("JSON parse error");
        return;
    }

    const char* imageUrl = jsonDoc["data"][0]["url"];
    if (imageUrl == nullptr) {
        Serial.println("Image URL not found in response");
        M5.Lcd.println("URL not found");
        return;
    }

    Serial.println("Generated Image URL:");
    Serial.println(imageUrl);

    // 生成された画像をダウンロードして表示
    displayImageFromURL(imageUrl);
}

// URLから画像をダウンロードして表示する関数
void displayImageFromURL(const char* url) {
    WiFiClientSecure client;
    client.setInsecure();

    Serial.println("Downloading image from URL:");
    Serial.println(url);

    // URLを解析
    String urlStr = String(url);
    int idx = urlStr.indexOf("/", 8); // "https://"をスキップ
    String host = urlStr.substring(8, idx);
    String path = urlStr.substring(idx);

    if (!client.connect(host.c_str(), 443)) {
        Serial.println("Connection to image server failed");
        M5.Lcd.println("Image download failed");
        return;
    }

    // GETリクエストを送信
    client.print(String("GET ") + path + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Connection: close\r\n\r\n");

    // HTTPヘッダーをスキップ
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") {
            break;
        }
    }

    // 画像データをバッファに読み込む
    uint8_t* buffer = (uint8_t*)ps_malloc(200 * 1024); // 200KBのメモリを確保
    if (!buffer) {
        Serial.println("Failed to allocate memory for image");
        M5.Lcd.println("Memory allocation failed");
        return;
    }

    size_t index = 0;
    while (client.connected() || client.available()) {
        if (client.available()) {
            int c = client.read();
            if (c < 0) break;
            buffer[index++] = (uint8_t)c;
            if (index >= 200 * 1024) {
                Serial.println("Image too large");
                M5.Lcd.println("Image too large");
                free(buffer);
                return;
            }
        }
    }
    client.stop();

    // 画像を表示
    int rc = png.openRAM(buffer, index, pngDraw);
    if (rc == PNG_SUCCESS) {
        M5.Lcd.clear();
        int decodeResult = png.decode(NULL, 0);
        if (decodeResult != 0) {
            Serial.printf("PNG decode failed with code: %d\n", decodeResult);
            M5.Lcd.println("Decode failed");
        } else {
            Serial.println("Edited image displayed successfully.");
        }
        png.close();
    } else {
        Serial.printf("PNG openRAM failed with code: %d\n", rc);
        M5.Lcd.println("Failed to open PNG");
    }
    free(buffer);
}

void setup() {
    Serial.begin(115200);
    M5.begin();
    M5.Lcd.setRotation(1);
    M5.Lcd.clear();
    M5.Lcd.println("Initializing...");

    // SDカードを初期化
    if (!SD.begin()) {
        Serial.println("SD Card initialization failed!");
        M5.Lcd.println("SD init failed");
        while (true);
    }

    // Wi-Fiに接続
    WiFi.begin(ssid, password);
    M5.Lcd.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        M5.Lcd.print(".");
    }
    M5.Lcd.println("\nWi-Fi connected");
    Serial.println("Wi-Fi connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // デフォルトの画像を表示
    displayImageFromSD("/elephant.png");
}

void loop() {
    M5.update();

    if (M5.BtnA.wasPressed()) {
        sendImageEditRequest();
    }
}
