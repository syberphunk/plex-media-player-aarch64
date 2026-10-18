import QtQuick
import Konvergo 1.0
import QtWebEngine
import QtWebChannel
import QtQuick.Window

KonvergoWindow
{
  id: mainWindow
  title: "Plex Media Player"
  objectName: "mainWindow"
  minimumHeight: windowMinSize.height
  minimumWidth: windowMinSize.width

  function runWebAction(action)
  {
    if (mainWindow.webDesktopMode)
      web.triggerWebAction(action)
  }

  Shortcut
  {
    enabled: mainWindow.webDesktopMode
    sequence: "F11"
    onActivated: mainWindow.toggleWebMode()
  }

  Shortcut
  {
    enabled: mainWindow.webDesktopMode
    sequence: "Shift+F11"
    onActivated: mainWindow.toggleFullscreen()
  }

  Shortcut
  {
    enabled: mainWindow.webDesktopMode
    sequences: [ StandardKey.Close ]
    onActivated: mainWindow.close()
  }

  Shortcut
  {
    enabled: mainWindow.webDesktopMode
    sequence: "Meta+Down"
    onActivated: mainWindow.minimizeWindow()
  }

  Shortcut
  {
    enabled: mainWindow.webDesktopMode
    sequences: [ StandardKey.Quit ]
    onActivated: mainWindow.close()
  }

  Shortcut
  {
    sequence: "Ctrl+Shift+D"
    enabled: mainWindow.webDesktopMode
    onActivated: mainWindow.toggleDebug()
  }

  Shortcut
  {
    sequences: [ StandardKey.Copy ]
    onActivated: runWebAction(WebEngineView.Copy)
    id: action_copy
  }

  Shortcut
  {
    sequences: [ StandardKey.Cut ]
    onActivated: runWebAction(WebEngineView.Cut)
    id: action_cut
  }

  Shortcut
  {
    sequences: [ StandardKey.Paste ]
    onActivated: runWebAction(WebEngineView.Paste)
    id: action_paste
  }

  Shortcut
  {
    sequences: [ StandardKey.SelectAll ]
    onActivated: runWebAction(WebEngineView.SelectAll)
    id: action_selectall
  }

  Shortcut
  {
    sequences: [ StandardKey.Undo ]
    onActivated: runWebAction(WebEngineView.Undo)
    id: action_undo
  }

  Shortcut
  {
    sequences: [ StandardKey.Redo ]
    onActivated: runWebAction(WebEngineView.Redo)
    id: action_redo
  }

  MpvVideo
  {
    id: video
    objectName: "video"
  }

  WebEngineProfilePrototype
  {
    id: webProfile
    storageName: "plexmediaplayer"
    httpCacheType: WebEngineProfile.DiskHttpCache
    persistentCookiesPolicy: WebEngineProfile.ForcePersistentCookies
  }

  WebEngineView
  {
    id: web
    objectName: "web"
    settings.errorPageEnabled: false
    settings.localContentCanAccessRemoteUrls: true
    focus: true
    property string currentHoveredUrl: ""
    onLinkHovered: (hoveredUrl) => { web.currentHoveredUrl = hoveredUrl }
    onUrlChanged: mainWindow.webClientNavigated(web.url)
    width: mainWindow.width
    height: mainWindow.height

    Component.onCompleted:
    {
      web.profile = webProfile.instance()
      web.profile.httpUserAgent = components.system.getUserAgent()

      var bridge = WebEngine.script()
      bridge.name = "qwebchannel"
      bridge.sourceUrl = "qrc:///qtwebchannel/qwebchannel.js"
      bridge.injectionPoint = WebEngineScript.DocumentCreation
      bridge.worldId = WebEngineScript.MainWorld
      web.userScripts.insert(bridge)

      var clientConfig = WebEngine.script()
      clientConfig.name = "plexweb"
      clientConfig.sourceCode = "window.PLEXWEB = Object.assign({}, window.PLEXWEB, " +
                                "{ allowLoopbackDiscovery: false });"
      clientConfig.injectionPoint = WebEngineScript.DocumentCreation
      clientConfig.worldId = WebEngineScript.MainWorld
      web.userScripts.insert(clientConfig)

      web.url = Qt.binding(function() { return mainWindow.webUrl })

      forceActiveFocus()
      mainWindow.reloadWebClient.connect(reload)
    }

    onLoadingChanged: (loadRequest) =>
    {
      // we use a timer here to switch to the webview since
      // it take a few moments for the webview to render
      // after it has loaded.
      //
      if (loadRequest.status == WebEngineLoadingInfo.LoadStartedStatus)
      {
        console.log("WebEngineLoadRequest starting: " + loadRequest.url);
      }
      else if (loadRequest.status == WebEngineLoadingInfo.LoadSucceededStatus)
      {
        console.log("WebEngineLoadRequest success: " + loadRequest.url);
      }
      else if (loadRequest.status == WebEngineLoadingInfo.LoadFailedStatus)
      {
        console.log("WebEngineLoadRequest failure: " + loadRequest.url + " error code: " + loadRequest.errorCode);
        errorLabel.visible = true
        errorLabel.text = "Error loading client, this is bad and should not happen<br>" +
                          "You can try to <a href='reload'>reload</a> or head to our <a href='http://plex.tv/support'>support page</a><br><br>Actual Error: <pre>" +
                          loadRequest.errorString + " [" + loadRequest.errorCode + "]</pre><br><br>" +
                          "Provide the <a target='_blank' href='file://"+ components.system.logFilePath + "'>logfile</a> as well."
      }
    }

    onNewWindowRequested: (request) =>
    {
      if (request.userInitiated)
      {
        var target = request.requestedUrl.toString()
        if (target.length == 0)
          target = web.currentHoveredUrl
        console.log("Opening external URL: " + target)
        components.system.openExternalUrl(target)
      }
    }

    onFullScreenRequested: (request) =>
    {
      console.log("Request fullscreen: " + request.toggleOn)
      mainWindow.setFullScreen(request.toggleOn)
      request.accept()
    }

    onJavaScriptConsoleMessage: (level, message, lineNumber, sourceID) =>
    {
      components.system.info(message)
    }

    onCertificateError: (error) =>
    {
      console.log(error.url + " :" + error.description + error.type)
      error.rejectCertificate()
    }
  }

  Text
  {
    id: errorLabel
    z: 5
    anchors.centerIn: parent
    color: "#999999"
    linkColor: "#cc7b19"
    text: "Generic error"
    font.pixelSize: 32
    font.bold: true
    visible: false
    verticalAlignment: Text.AlignVCenter
    textFormat: Text.StyledText
    onLinkActivated: (link) =>
    {
      if (link == "reload")
      {
        errorLabel.visible = false
        web.reload()
      }
      else
      {
        Qt.openUrlExternally(link)
      }
    }
  }

  Rectangle
  {
    id: debug
    color: "black"
    z: 10
    anchors.centerIn: parent
    width: parent.width
    height: parent.height
    opacity: 0.7
    visible: mainWindow.showDebugLayer

    Text
    {
      id: debugLabel
      width: (parent.width - 50) / 2
      height: parent.height - 25
      anchors.left: parent.left
      anchors.leftMargin: 64
      anchors.top: parent.top
      anchors.topMargin: 54
      anchors.bottomMargin: 54
      color: "white"
      font.pixelSize: Math.round(height / 65)
      wrapMode: Text.WrapAnywhere

      function windowDebug()
      {
        var dbg = mainWindow.debugInfo + "Window and web\n";
        dbg += "  Window size: " + parent.width + "x" + parent.height + " - " + web.width + "x" + web.height + "\n";
        dbg += "  DevicePixel ratio: " + Screen.devicePixelRatio + "\n";

        return dbg;
      }

      text: windowDebug()
    }

    Text
    {
      id: videoLabel
      width: (parent.width - 50) / 2
      height: parent.height - 25
      anchors.right: parent.right
      anchors.left: debugLabel.right
      anchors.rightMargin: 64
      anchors.top: parent.top
      anchors.topMargin: 54
      anchors.bottomMargin: 54
      color: "white"
      font.pixelSize: Math.round(height / 65)
      wrapMode: Text.WrapAnywhere

      text: mainWindow.videoInfo
    }
  }

  property QtObject webChannel: web.webChannel
}
