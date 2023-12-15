#ifndef WITHDRAW_LNURLW_H
#define WITHDRAW_LNURLW_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "bech32.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "config.h"

struct WithdrawRequest {
    String status;
    String reason;

    String tag;
    String callback;
    String k1;
    long minWithdrawable;
    long maxWithdrawable;
    String defaultDescription;
};

struct ConversionResult {
    float Fiat;
    int sats;
    float BTC;
};

String sendHttpGet(String url);
String sendHttpPost(String url, String postParams);
ConversionResult convertFiatToSats(const double &amount);
String decodeLnurlw(String lnurlw);
WithdrawRequest parseWithdrawRequest(String json);
String createInvoice(String callbackUrl, String k1, const double &amount);
bool requestLnurlwServerToPayInvoice(String invoice, String callbackUrl, String k1);
bool withdrawFromLnurlw(String lnurlw, const std::string &invoice);

#endif // WITHDRAW_LNURLW_H