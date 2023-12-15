#include "withdraw_lnurlw.h"

WithdrawRequest parseWithdrawRequest(String json) {
    // The JSON document
    StaticJsonDocument<512> doc;

    // Parse the JSON string into the document
    DeserializationError error = deserializeJson(doc, json);
    if (error) {
        Serial.println(F("Failed to read JSON"));
        // return some kind of error state here if needed
    }

    // Create a new WithdrawRequest struct
    WithdrawRequest request;

    if (doc.containsKey("status")) {
        // Extract error fields from the JSON document
        request.status = doc["status"].as<String>();
        request.reason = doc["reason"].as<String>();
    } else {
        // Extract withdrawRequest fields from the JSON document
        request.tag = doc["tag"].as<String>();
        request.callback = doc["callback"].as<String>();
        request.k1 = doc["k1"].as<String>();
        request.minWithdrawable = doc["minWithdrawable"].as<long>();
        request.maxWithdrawable = doc["maxWithdrawable"].as<long>();
        request.defaultDescription = doc["defaultDescription"].as<String>();
    }

    // Return the populated struct
    return request;
}


// Function to send a GET request and return the response body
String sendHttpGet(String url) {
    WiFiClientSecure client;

    // Connect to the host (URL is in the format "https://host/path")
    String host = url.substring(url.indexOf("//") + 2);
    host = host.substring(0, host.indexOf("/"));
    String path = url.substring(url.indexOf(host) + host.length());
    if (!client.connect(host.c_str(), 443)) {
        Serial.println("Connection failed.");
        return "";
    }

    // Send the HTTP GET request
    client.print(String("GET ") + path + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Connection: close\r\n\r\n");

    // Wait for the response to become available
    while (client.connected()) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") {
                break;
            }
        }
    }

    // Read the body of the response
    String responseBody = client.readString();
    return responseBody;
}

// Function to send a POST request and return the response body
String sendHttpPost(String url, String postParams) {
    WiFiClientSecure client;

    // Connect to the host (URL is in the format "https://host/path")
    String host = url.substring(url.indexOf("//") + 2);
    host = host.substring(0, host.indexOf("/"));
    String path = url.substring(url.indexOf(host) + host.length());
    if (!client.connect(host.c_str(), 443)) {
        Serial.println("Connection failed.");
        return "";
    }

    // Send the HTTP POST request
    client.print(String("POST ") + path + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Content-Type: application/x-www-form-urlencoded\r\n" +
                 "Content-Length: " + postParams.length() + "\r\n\r\n" +
                 postParams);

    // Wait for the response to become available
    while (client.connected()) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") {
                break;
            }
        }
    }

    // Read the body of the response
    String responseBody = client.readString();
    return responseBody;
}

ConversionResult convertFiatToSats(const double &amount) {
    // Construct the request body
    String fiatCur = config::getString("fiatCurrency").c_str();
    StaticJsonDocument<100> reqDoc;
    reqDoc["from"] = fiatCur;
    reqDoc["amount"] = amount;
    reqDoc["to"] = "sat";
    String requestBody;
    serializeJson(reqDoc, requestBody);

    Serial.println("Request body for Fiat to sat conversion: " + requestBody);

    // Initialize the HTTPClient
    HTTPClient http;

    // Begin session with the url
    http.begin("https://lnbits.michaelantonfischer.com/api/v1/conversion");

    // Specify request headers
    http.addHeader("Content-Type", "application/json");

    // Send the request and get the response
    int httpCode = http.POST(requestBody);
    String response = http.getString();
    Serial.println("Response for Fiat to sat conversion: " + response);

    // Check the HTTP response code
    if (httpCode != HTTP_CODE_OK) {
        Serial.print("HTTP request failed with status code ");
        Serial.println(httpCode);
        // return some kind of error state here if needed
    }

    // Parse the response
    StaticJsonDocument<100> resDoc;
    DeserializationError error = deserializeJson(resDoc, response);
    if (error) {
        Serial.println(F("Failed to parse JSON response"));
        // return some kind of error state here if needed
    }

    // Construct and return the result
    ConversionResult result;
    result.Fiat = resDoc[fiatCur].as<float>();
    result.sats = resDoc["sats"].as<int>();
    result.BTC = resDoc["BTC"].as<float>();
    return result;
}

String decodeLnurlw(String lnurlw) {
    HTTPClient http;
    String payload = "";

    http.begin(lnurlw); //Specify request destination
    int httpCode = http.GET(); //Send the request

    if (httpCode > 0) { //Check the returning code
        payload = http.getString(); //Get the request response payload
        Serial.println(payload); //Print the response payload
        
    }

    http.end(); //Close connection
    return payload; //Return the response payload
}

String createInvoice(String callbackUrl, String k1, const double &amount) {
    HTTPClient http;
    int amountSats = convertFiatToSats(amount).sats;
    int amountMsats = amountSats * 1000; //convert to msats

    // Create JSON object for POST data
    DynamicJsonDocument jsonDoc(1024);
    jsonDoc["amount"] = amountMsats;
    jsonDoc["k1"] = k1;
    String postData;
    serializeJson(jsonDoc, postData);
    Serial.println("Create invoice POST data: " + postData);

    http.begin(callbackUrl); //Specify request destination
    http.addHeader("Content-Type", "application/json"); //Specify content-type header

    int httpCode = http.POST(postData); //Send the request
    String payload = http.getString(); //Get the response payload

    Serial.println("Create invoice response: " + payload); //Print the response payload
    Serial.println("POST request sent to: " + callbackUrl + " with data: " + postData); //Print the request details

    http.end(); //Close connection
    
    if (httpCode == 200) { // Check the returning code
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);
        String pr = doc["pr"].as<String>();
        return pr;
    } else {
        Serial.println("Error on HTTP request");
        return "Error";
    }
}

bool requestLnurlwServerToPayInvoice(String invoice, String callbackUrl, String k1) {
    HTTPClient http;
  
    // Manually encode special characters in k1
    String k1_encoded = "";
    for (char c : k1) {
        if (isalnum(c)) {
            k1_encoded += c;
        } else {
            char encoded[4];
            sprintf(encoded, "%%%02X", c);
            k1_encoded += encoded;
        }
    }

    // Check if callbackUrl already contains query parameters
    if (callbackUrl.indexOf('?') != -1) {
        // callbackUrl already contains query parameters, append with '&'
        callbackUrl += "&k1=" + k1_encoded + "&pr=" + invoice;
    } else {
        // callbackUrl doesn't contain query parameters, append with '?'
        callbackUrl += "?k1=" + k1_encoded + "&pr=" + invoice;
    }

    http.begin(callbackUrl); // Specify request destination
    int httpCode = http.GET(); // Send the request
    String payload = http.getString(); // Get the response payload
    
    Serial.println("Response: " + payload); // Print the response payload
    Serial.println("GET request sent to: " + callbackUrl); //Print the request details

    http.end(); // Close connection
    if (httpCode == 200) { // Check the returning code
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);
        String status = doc["status"].as<String>();
        if (status == "OK") {
            return true;
        }
    }
    return false;
}

bool withdrawFromLnurlw(String lnurlw, const std::string &invoice) {
    bool isSuccess = false;
    // Decode the lnurlw to get a URL
    Serial.println("Trying to withdraw from LNURLw: " + lnurlw);
    String boltcardJSON = decodeLnurlw(lnurlw);
    Serial.println("LNURLw decoded: " + boltcardJSON);
    WithdrawRequest request = parseWithdrawRequest(boltcardJSON);

    Serial.println("URL decoded from LNURLw: " + request.callback);
    Serial.println("K1 decoded from LNURLw: " + request.k1);

    // Check if K1 contains special characters
    for (char c : request.k1) {
        if (!isalnum(c)) {
            Serial.println("K1 contains special characters. This might cause issues.");
        }
    }

    // Create a Lightning invoice for the max withdrawable amount
    isSuccess = requestLnurlwServerToPayInvoice(invoice.c_str(), request.callback, request.k1);
    
    return isSuccess;
}
