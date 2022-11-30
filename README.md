# VirtualCaptionCam
字幕つき仮想カメラ (Virtual camera with automated caption)

Zoomなどで使える仮想カメラと、仮想カメラのターゲットとして動作するブラウザ（字幕カメラに自動でアクセス）を組み合わせたツール。

「字幕カメラでビデオ会議に参加」を極力簡単にしようと作成したものの、ブラウザの元であるMicrosoft EdgeがPCによっては音声認識できないらしいと判明したため、とりあえず開発中断。

（一応、リポジトリのコードは動作するものです）

# 参考：ビルドについて

ビルドには、別途DirectShowのbaseclassesをビルドしておく必要があります。本リポジトリでは、下記のコードを `c:\dev\sdk71examples` の下においてビルドしていることを前提としています。他のディレクトリの場合は、適宜プロジェクトのincludeを変更してください。

https://github.com/ganboing/sdk71examples
