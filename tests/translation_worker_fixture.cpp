#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>
#include <cstdio>
#include <memory>

// Test-only wire-protocol fixture. No model, credentials or commercial text.
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    const auto value=[&](const QString &name){const auto index=args.indexOf(name);return index>=0&&index+1<args.size()?args[index+1]:QString();};
    const auto model=value("--model"),token=value("--api-key"),device=value("--device");
    if(value("--host")!="127.0.0.1"||!args.contains("--offline")||token.isEmpty())return 20;
    const bool npu=qEnvironmentVariable("GGML_OPENVINO_DEVICE")=="NPU";
    const bool npuBudgetFixture=npu && model.contains("fixture-npu-budget");
    if(device=="Vulkan8" && (value("--n-gpu-layers")!="12" || value("--flash-attn")!="off"))return 8;
    if((npu && !npuBudgetFixture)||device=="Vulkan7"){
        std::fprintf(stderr,"OpenVINO NPU/device initialization failed: fixture unavailable device\n");return 1;
    }
    QFile started(model+".starts");if(started.open(QIODevice::Append)){started.write("started\n");started.close();}
    if(npuBudgetFixture)std::fprintf(stderr,"OpenVINO: using device NPU\nmodel loaded\n");
    else if(device=="none")std::fprintf(stderr,"llama_context: CPU compute buffer size = 32 MiB\n");
    else if(device=="Vulkan8")std::fprintf(stderr,"using device Vulkan8 (Fixture GPU)\noffloaded 12/29 layers to GPU\n");
    else std::fprintf(stderr,"llama_prepare_model_devices: using device Vulkan0 (Fixture GPU)\nload_tensors: offloaded 29/29 layers to GPU\n");
    std::fflush(stderr);
    QTcpServer server;if(!server.listen(QHostAddress::LocalHost,value("--port").toUShort()))return 21;
    QObject::connect(&server,&QTcpServer::newConnection,&app,[&]{
        while(auto *socket=server.nextPendingConnection()){
            auto input=std::make_shared<QByteArray>();
            QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket,input]{
                input->append(socket->readAll());const auto headerEnd=input->indexOf("\r\n\r\n");if(headerEnd<0)return;
                const auto headers=input->left(headerEnd);int length=0;
                for(const auto &line:headers.split('\n'))if(line.toLower().startsWith("content-length:"))length=line.mid(15).trimmed().toInt();
                if(input->size()<headerEnd+4+length)return;
                QObject::disconnect(socket,&QTcpSocket::readyRead,socket,nullptr);
                const bool health=headers.startsWith("GET /health ");
                if(!health&&!headers.contains("Bearer "+token.toUtf8())){socket->disconnectFromHost();return;}
                const auto payload=QJsonDocument::fromJson(input->mid(headerEnd+4,length)).object();
                const auto messages=payload.value("messages").toArray();
                const bool plain=!payload.contains("response_format");
                auto source=messages.isEmpty()?QString():messages.last().toObject().value("content").toString();
                QJsonObject data;
                if(plain){
                    const QString prefix="[Background Information]\n",marker="\n\n[Source Text]\n";
                    const auto background=QJsonDocument::fromJson(source.mid(prefix.size(),source.indexOf("\n\n")-prefix.size()).toUtf8()).object();
                    const auto decoded=QJsonDocument::fromJson(("["+source.mid(source.lastIndexOf(marker)+marker.size())+"]").toUtf8()).array();
                    data={{"context",background.value("context")},{"target",QJsonObject{{"start_ms",background.value("target_start_ms")},{"end_ms",background.value("target_end_ms")},{"text",decoded.isEmpty()?QJsonValue{}:decoded[0]}}}};
                }else data=QJsonDocument::fromJson(source.toUtf8()).object();
                if(data.value("target").toObject().value("text").isString())source=data.value("target").toObject().value("text").toString();
                if(!health){QFile requests(model+".requests");if(requests.open(QIODevice::Append))requests.write(QJsonDocument(QJsonObject{{"source",source},{"plain",plain},{"npu",npu},{"data",data}}).toJson(QJsonDocument::Compact)+"\n");}
                QPointer<QTcpSocket> safe(socket);
                QTimer::singleShot(health&&model.contains("boot")?250:(source==QString::fromUtf8("遅い") || source=="delayed-invalid")?350:5,&app,[safe,health,source,plain]{
                    if(!safe||safe->state()!=QAbstractSocket::ConnectedState)return;
                    const auto korean=source=="bad"?QString::fromUtf8("日本語です。"):source=="context-dump"?QString(200,QChar(0xac00)):plain?QString::fromUtf8("가지 않아도 돼."):QString::fromUtf8("안녕하세요.");
                    const auto content=plain?korean:QString::fromUtf8(QJsonDocument(QJsonObject{{"korean",korean}}).toJson(QJsonDocument::Compact));
                    const auto finish=source=="truncated" || source=="delayed-invalid"?"length":"stop";
                    auto result=health?QJsonObject{{"status","ok"}}:QJsonObject{{"choices",QJsonArray{QJsonObject{{"finish_reason",finish},{"message",QJsonObject{{"role","assistant"},{"content",content}}}}}}};
                    if(source=="oversized-response")result.insert("padding",QString(300000,'x'));
                    const auto body=source=="malformed"?QByteArray("{\"choices\":"):QJsonDocument(result).toJson(QJsonDocument::Compact);
                    const QByteArray status=source=="rejected-request"?"HTTP/1.1 413 Payload Too Large":"HTTP/1.1 200 OK";
                    const auto declaredSize=body.size()+(source=="cutoff-response"?10:0);
                    safe->write(status+"\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(declaredSize)+"\r\nConnection: close\r\n\r\n"+body);safe->disconnectFromHost();
                });
            });
        }
    });
    return app.exec();
}
