/*
   Copyright 2016 Wang Haomiao<et.tw@163.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */
#include <QObject>
#include <QEventLoop>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QBuffer>
#include "SeimiServerHandler.h"
#include "SeimiWebPage.h"
#include "pillowcore/HttpServer.h"
#include "pillowcore/HttpHandler.h"
#include "pillowcore/HttpConnection.h"

SeimiServerHandler::SeimiServerHandler(QObject *parent):Pillow::HttpHandler(parent),
    renderTimeP("renderTime"),
    urlP("url"),
    proxyP("proxy"),
    scriptP("script"),
    useCookieP("useCookie"),
    postParamP("postParam"),
    contentTypeP("contentType"),
    outImgSizeP("outImgSize"),
    uaP("ua")
{

}

bool SeimiServerHandler::handleRequest(Pillow::HttpConnection *connection){
    QString method = connection->requestMethod();
    QString path = connection->requestPath();
    if(method == "GET"){
        connection->writeResponse(405, Pillow::HttpHeaderCollection(),"Method 'GET' is not supprot,please use 'POST'");
        return true;
    }
    if(path != "/doload"){
        return false;
    }
    QEventLoop eventLoop;
    SeimiPage *seimiPage=new SeimiPage(this);
    QString url = QUrl::fromPercentEncoding(connection->requestParamValue(urlP).toUtf8());
    int renderTime = connection->requestParamValue(renderTimeP).toInt();
    QString proxyStr = connection->requestParamValue(proxyP);
    QString contentType = connection->requestParamValue(contentTypeP);
    QString outImgSizeStr = connection->requestParamValue(outImgSizeP);
    QString ua = connection->requestParamValue(uaP);
    if(!proxyStr.isEmpty()){
        QRegularExpression reProxy("(?<protocol>http|https|socket)://(?:(?<user>\\w*):(?<password>\\w*)@)?(?<host>[\\w.]+)(:(?<port>\\d+))?");
        QRegularExpressionMatch matchProxy = reProxy.match(proxyStr);
        if(matchProxy.hasMatch()){
            QNetworkProxy proxy;
            if(matchProxy.captured("protocol") == "socket"){
                proxy.setType(QNetworkProxy::Socks5Proxy);
            }else{
                proxy.setType(QNetworkProxy::HttpProxy);
            }
            proxy.setHostName(matchProxy.captured("host"));
            proxy.setPort(matchProxy.captured("port").toInt()==0?80:matchProxy.captured("port").toInt());
            proxy.setUser(matchProxy.captured("user"));
            proxy.setPassword(matchProxy.captured("password"));

            seimiPage->setProxy(proxy);
        }else {
            qWarning("[seimi] proxy pattern error, proxy = %s",proxyStr.toUtf8().constData());
        }
    }

    QString jscript = QUrl::fromPercentEncoding(connection->requestParamValue(scriptP).toUtf8());
//    qDebug()<<"recive js:"<<jscript;
    QString postParamJson = connection->requestParamValue(postParamP);
    seimiPage->setScript(jscript);
    seimiPage->setPostParam(postParamJson);
    qInfo("[seimi] TargetUrl:%s ,RenderTime(ms):%d",url.toUtf8().constData(),renderTime);
    int useCookieFlag = connection->requestParamValue(useCookieP).toInt();
    seimiPage->setUseCookie(useCookieFlag==1);
    QObject::connect(seimiPage,SIGNAL(loadOver()),&eventLoop,SLOT(quit()));
    seimiPage->toLoad(url,renderTime,ua);
    eventLoop.exec();
    Pillow::HttpHeaderCollection headers;
    headers << Pillow::HttpHeader("Pragma", "no-cache");
    headers << Pillow::HttpHeader("Expires", "-1");
    headers << Pillow::HttpHeader("Cache-Control", "no-cache");
    if(contentType == "pdf"){
        headers << Pillow::HttpHeader("Content-Type", "application/pdf");
        QByteArray pdfContent = seimiPage->generatePdf();
        QCryptographicHash md5sum(QCryptographicHash::Md5);
        md5sum.addData(pdfContent);
        QByteArray etag = md5sum.result().toHex();
        headers << Pillow::HttpHeader("ETag", etag);
        connection->writeResponse(200,headers,pdfContent);
    }else if(contentType == "img"){
        headers << Pillow::HttpHeader("Content-Type", "image/png");
        QSize targetSize;
        if(!outImgSizeStr.isEmpty()){
            QRegularExpression reImgSize("(?<xSize>\\d+)(?:x|X)(?<ySize>\\d+)");
            QRegularExpressionMatch matchImgSize = reImgSize.match(outImgSizeStr);
            if(matchImgSize.hasMatch()){
                targetSize.setWidth(matchImgSize.captured("xSize").toInt());
                targetSize.setHeight(matchImgSize.captured("ySize").toInt());
            }
        }
        QByteArray imgContent = seimiPage->generateImg(targetSize);
        QCryptographicHash md5sum(QCryptographicHash::Md5);
        md5sum.addData(imgContent);
        QByteArray etag = md5sum.result().toHex();
        headers << Pillow::HttpHeader("ETag", etag);
        connection->writeResponse(200,headers,imgContent);
    }else{
        headers << Pillow::HttpHeader("Content-Type", "text/html;charset=utf-8");
        QString defBody = "<html>null</html>";
        qInfo()<<"content:"<<seimiPage->getContent();
        connection->writeResponse(200, headers,seimiPage->getContent().isEmpty()?defBody.toUtf8():seimiPage->getContent().toUtf8());
    }
    seimiPage->deleteLater();
    return true;
}

