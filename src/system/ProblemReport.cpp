#include "ProblemReport.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <functional>
#include <zlib.h>

#include "Paths.h"

namespace
{
  const int kPictureLimit = 24;
  const qint64 kLogLimit = 8 * 1024 * 1024;

  struct KindInfo
  {
    const char* kind;
    const char* prefix;
    const char* label;
  };

  const KindInfo kKinds[] = {
    {"token", "[token]", "Plex tokens"},
    {"server", "server-", "server names"},
    {"title", "title-", "titles"},
    {"tags", "[media tags removed]", "lines of media tags"},
    {"id", "id-", "server, device and session IDs"},
    {"host", "host-", "plex.direct host names"},
    {"lan-ip", "lan-ip-", "local IP addresses"},
    {"wan-ip", "wan-ip-", "internet IP addresses"},
    {"address", "address-", "IPv6 and hardware addresses"},
    {"user", "user-", "account names and email addresses"},
    {"device", "device-", "device names"},
    {"home", "/home/USER", "home folder names"},
    {"steam", "steam-user", "Steam account numbers"},
    {"item", "item-", "library item numbers"},
    {"file", "file-", "file names"},
  };

  const QStringList kNotServerNames = {
    "plex", "plex.tv", "myplex", "home", "server", "local", "localhost", "frame", "steamdeck"
  };

  using Substitute = std::function<QString(const QRegularExpressionMatch&)>;

  QString substitute(const QString& text, const QRegularExpression& re, const Substitute& fn)
  {
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(text);
    if (!it.hasNext())
      return text;

    out.reserve(text.size());
    while (it.hasNext())
    {
      const QRegularExpressionMatch match = it.next();
      out += QStringView(text).mid(last, match.capturedStart() - last);
      out += fn(match);
      last = match.capturedEnd();
    }
    out += QStringView(text).mid(last);
    return out;
  }

  bool isLocalIp(int a, int b)
  {
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
           (a == 169 && b == 254) || (a == 100 && b >= 64 && b <= 127);
  }

  QByteArray readCapped(const QString& path, qint64 limit)
  {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
      return QByteArray();
    if (file.size() > limit)
      file.seek(file.size() - limit);
    return file.readAll();
  }

  class TarGzWriter
  {
  public:
    explicit TarGzWriter(const QString& path) : m_file(path)
    {
      m_stream.zalloc = Z_NULL;
      m_stream.zfree = Z_NULL;
      m_stream.opaque = Z_NULL;
      m_ok = m_file.open(QIODevice::WriteOnly) &&
             deflateInit2(&m_stream, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) == Z_OK;
      m_zlib = m_ok;
    }

    ~TarGzWriter()
    {
      if (m_zlib)
        deflateEnd(&m_stream);
    }

    bool ok() const { return m_ok; }
    QString error() const { return m_file.errorString(); }

    void add(const QString& name, const QByteArray& data, qint64 mtime)
    {
      QByteArray header(512, '\0');
      const QByteArray path = name.toUtf8().left(99);
      memcpy(header.data(), path.constData(), path.size());
      memcpy(header.data() + 100, "0000644", 7);
      memcpy(header.data() + 108, "0000000", 7);
      memcpy(header.data() + 116, "0000000", 7);
      const QByteArray size = QByteArray::number(data.size(), 8).rightJustified(11, '0');
      memcpy(header.data() + 124, size.constData(), 11);
      const QByteArray time = QByteArray::number(mtime, 8).rightJustified(11, '0');
      memcpy(header.data() + 136, time.constData(), 11);
      memset(header.data() + 148, ' ', 8);
      header[156] = '0';
      memcpy(header.data() + 257, "ustar", 6);
      memcpy(header.data() + 263, "00", 2);

      unsigned int sum = 0;
      for (char c : header)
        sum += static_cast<unsigned char>(c);
      const QByteArray check = QByteArray::number(sum, 8).rightJustified(6, '0');
      memcpy(header.data() + 148, check.constData(), 6);
      header[154] = '\0';
      header[155] = ' ';

      deflateBytes(header, Z_NO_FLUSH);
      deflateBytes(data, Z_NO_FLUSH);
      const int pad = (512 - data.size() % 512) % 512;
      if (pad)
        deflateBytes(QByteArray(pad, '\0'), Z_NO_FLUSH);
    }

    bool finish()
    {
      deflateBytes(QByteArray(1024, '\0'), Z_FINISH);
      return m_ok && m_file.commit();
    }

  private:
    void deflateBytes(const QByteArray& data, int flush)
    {
      if (!m_ok)
        return;

      m_stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
      m_stream.avail_in = static_cast<uInt>(data.size());
      char buffer[64 * 1024];
      int result = Z_OK;
      do
      {
        m_stream.next_out = reinterpret_cast<Bytef*>(buffer);
        m_stream.avail_out = sizeof(buffer);
        result = deflate(&m_stream, flush);
        if (result == Z_STREAM_ERROR)
        {
          m_ok = false;
          return;
        }
        const qint64 produced = qint64(sizeof(buffer)) - m_stream.avail_out;
        if (produced && m_file.write(buffer, produced) != produced)
        {
          m_ok = false;
          return;
        }
      } while (m_stream.avail_out == 0 || (flush == Z_FINISH && result != Z_STREAM_END));
    }

    QSaveFile m_file;
    z_stream m_stream {};
    bool m_ok = false;
    bool m_zlib = false;
  };
}

/////////////////////////////////////////////////////////////////////////////////////////
void ReportRedactor::learnValue(const QString& kind, const QString& value)
{
  const QString trimmed = value.trimmed();
  if (trimmed.isEmpty())
    return;

  if (kind == QLatin1String("server"))
  {
    if (trimmed.size() < 3 || trimmed.size() > 64 || trimmed.startsWith('[') ||
        kNotServerNames.contains(trimmed.toLower()))
      return;
  }
  else if (kind == QLatin1String("title"))
  {
    if (trimmed.size() < 4)
      return;
  }
  else if (trimmed.size() < 8)
  {
    return;
  }

  QStringList& values = m_learned[kind];
  if (!values.contains(trimmed))
    values << trimmed;
}

/////////////////////////////////////////////////////////////////////////////////////////
void ReportRedactor::learn(const QString& text)
{
  static const QRegularExpression serverInit(
    "Initialize server with(?:out)? token, ([^,\\n]+), ([0-9a-f]{40})");
  static const QRegularExpression serverConnection(
    "connection (?:to|for) ([^\\n]+?) (?:is|at) (?:https?|wss?)://");
  static const QRegularExpression serverNamed(
    "\\[(?:Servers|Primary Server|Connections)\\] ([^\\n\\[]+?) "
    "(?:was found through|is now the primary server|connected at)");
  static const QRegularExpression serverList("Found all servers = ([^\\n]+)");
  static const QRegularExpression title(
    "display-tags: +Title: ([^\\n]+?)\\s*$", QRegularExpression::MultilineOption);
  static const QRegularExpression identifier(
    "X-Plex-(?:Client-Identifier|Session-Identifier|Session-Id|Playback-Id|"
    "Playback-Session-Id)=([^&\\s\"'<>]+)");
  static const QRegularExpression clientUuid("\"clientUUID\"\\s*:\\s*\"([^\"]+)\"");

  for (auto it = serverInit.globalMatch(text); it.hasNext();)
  {
    const auto match = it.next();
    learnValue("server", match.captured(1));
    learnValue("id", match.captured(2));
  }
  for (const auto* re : {&serverConnection, &serverNamed})
  {
    for (auto it = re->globalMatch(text); it.hasNext();)
      learnValue("server", it.next().captured(1));
  }
  for (auto it = serverList.globalMatch(text); it.hasNext();)
  {
    for (const QString& name : it.next().captured(1).split(", "))
      learnValue("server", name);
  }
  for (auto it = title.globalMatch(text); it.hasNext();)
    learnValue("title", it.next().captured(1));
  for (const auto* re : {&identifier, &clientUuid})
  {
    for (auto it = re->globalMatch(text); it.hasNext();)
      learnValue("id", it.next().captured(1));
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
QString ReportRedactor::standIn(const QString& kind, const QString& value)
{
  Kind& entry = m_kinds[kind];
  if (entry.prefix.isEmpty())
  {
    for (const KindInfo& info : kKinds)
    {
      if (kind == QLatin1String(info.kind))
        entry.prefix = QString::fromLatin1(info.prefix);
    }
  }

  entry.places++;
  if (!entry.prefix.endsWith('-'))
    return entry.prefix;

  auto found = entry.standIns.constFind(value);
  if (found != entry.standIns.constEnd())
    return found.value();

  const QString name = entry.prefix + QString::number(entry.standIns.size() + 1);
  entry.standIns.insert(value, name);
  return name;
}

/////////////////////////////////////////////////////////////////////////////////////////
QString ReportRedactor::replaceLearned(const QString& text, const QString& kind, bool wholeWord)
{
  QStringList values = m_learned.value(kind);
  if (values.isEmpty())
    return text;

  std::sort(values.begin(), values.end(),
            [](const QString& a, const QString& b) { return a.size() > b.size(); });

  QStringList escaped;
  for (const QString& value : values)
    escaped << QRegularExpression::escape(value);

  QString pattern = "(?:" + escaped.join('|') + ")";
  if (wholeWord)
    pattern = "(?<![\\w.-])" + pattern + "(?![\\w-])";

  const QRegularExpression re(pattern);
  return substitute(text, re, [&](const QRegularExpressionMatch& match) {
    return standIn(kind, match.captured(0));
  });
}

/////////////////////////////////////////////////////////////////////////////////////////
QString ReportRedactor::replaceIp(const QString& ip)
{
  const QStringList parts = ip.split('.');
  const int a = parts.value(0).toInt();
  const int b = parts.value(1).toInt();
  return standIn(isLocalIp(a, b) ? "lan-ip" : "wan-ip", ip);
}

/////////////////////////////////////////////////////////////////////////////////////////
QString ReportRedactor::redact(const QString& input)
{
  static const QRegularExpression tags(
    "(display-tags: )(?!File tags:)[^\\n]+", QRegularExpression::MultilineOption);
  static const QRegularExpression token(
    "((?:x-plex-token|auth_?token|access_?token|authentication_?token|(?<![a-z])token)"
    "(?:=|%3D|\"?\\s*:\\s*\"|=\\\\?\"))([^&\\s\"'<>,;\\\\)]+)",
    QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression plexDirect(
    "(?:(?<![\\w.-])|(?<=%2F))(\\d{1,3}(?:-\\d{1,3}){3})\\.([0-9a-f]{32})\\.plex\\.direct",
    QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression email(
    "[A-Za-z0-9._+-]+(?:@|%40)[A-Za-z0-9-]+(?:\\.[A-Za-z0-9-]+)*\\.[A-Za-z]{2,}");
  static const QRegularExpression hexId(
    "(?<![\\w-])(?:[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}|"
    "[0-9a-fA-F]{40}|[0-9a-f]{32})(?![\\w-])");
  static const QRegularExpression ipv4(
    "(?:(?<![\\w.-])|(?<=%2F)|(?<=%3D))(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})"
    "(?![\\w-]|\\.\\d)",
    QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression ipv6(
    "(?<![\\w:.%-])[0-9A-Fa-f:]*:[0-9A-Fa-f:]*:[0-9A-Fa-f:]*(?![\\w:])");
  static const QRegularExpression mac("^[0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}$");
  static const QRegularExpression deviceParam("(X-Plex-Device-Name=)([^&\\s\"'<>]+)");
  static const QRegularExpression accountParam("(X-Plex-Account-ID=)([^&\\s\"'<>]+)");
  static const QRegularExpression systemName("(\"systemname\"\\s*:\\s*\")([^\"]+)");
  static const QRegularExpression home("/home/([^/\\s\"'<>]+)");
  static const QRegularExpression socket("(pmp_[A-Za-z]+_)([^.\\s\"'/]+)(\\.sock)");
  static const QRegularExpression userdata("(userdata/)(\\d+)");
  static const QRegularExpression steamId("(?<!\\d)7656119\\d{10}(?!\\d)");
  static const QRegularExpression item(
    "((?:/|%2F)library(?:/|%2F)(?:metadata|parts)(?:/|%2F)|subscriptions/mapping/|"
    "ratingKey=|\"ratingKey\"\\s*:\\s*\"?)(\\d+(?:,\\d+)*)",
    QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression fileName(
    "(?<![\\w.%-])([^\\s/\\\\\"'?&=<>|:]+)\\.(mkv|mp4|m4v|avi|mov|wmv|ts|m2ts|webm|flv|iso|"
    "mpg|mpeg|srt|ass)(?!\\w)",
    QRegularExpression::CaseInsensitiveOption);

  QString text = substitute(input, tags, [&](const QRegularExpressionMatch& m) {
    return m.captured(1) + standIn("tags", QString());
  });

  text = substitute(text, token, [&](const QRegularExpressionMatch& m) {
    return m.captured(1) + standIn("token", QString());
  });

  text = replaceLearned(text, "title", false);
  text = replaceLearned(text, "server", true);
  text = replaceLearned(text, "id", false);

  text = substitute(text, plexDirect, [&](const QRegularExpressionMatch& m) {
    const QString ip = m.captured(1).replace('-', '.');
    return replaceIp(ip) + "." + standIn("host", m.captured(2).toLower()) + ".plex.direct";
  });

  text = substitute(text, email, [&](const QRegularExpressionMatch& m) {
    return standIn("user", m.captured(0).replace("%40", "@").toLower());
  });

  text = substitute(text, hexId, [&](const QRegularExpressionMatch& m) {
    return standIn("id", m.captured(0).toLower());
  });

  text = substitute(text, ipv4, [&](const QRegularExpressionMatch& m) {
    for (int n = 1; n <= 4; n++)
    {
      if (m.captured(n).toInt() > 255)
        return m.captured(0);
    }
    const int a = m.captured(1).toInt();
    if (a == 127 || a == 0 || a >= 224)
      return m.captured(0);
    return replaceIp(m.captured(0));
  });

  text = substitute(text, ipv6, [&](const QRegularExpressionMatch& m) {
    const QString candidate = m.captured(0);
    if (mac.match(candidate).hasMatch())
      return standIn("address", candidate.toLower());

    QHostAddress address;
    if (candidate.count(':') < 2 || !address.setAddress(candidate) ||
        address.protocol() != QAbstractSocket::IPv6Protocol || address.isLoopback() ||
        address.isNull() || address == QHostAddress::AnyIPv6)
      return candidate;
    return standIn("address", address.toString());
  });

  text = substitute(text, deviceParam, [&](const QRegularExpressionMatch& m) {
    return m.captured(1) + standIn("device", m.captured(2));
  });

  text = substitute(text, accountParam, [&](const QRegularExpressionMatch& m) {
    return m.captured(1) + standIn("user", "account:" + m.captured(2));
  });

  text = substitute(text, systemName, [&](const QRegularExpressionMatch& m) {
    if (m.captured(2) == QLatin1String("PlexMediaPlayer"))
      return m.captured(0);
    return m.captured(1) + standIn("device", m.captured(2));
  });

  text = substitute(text, home, [&](const QRegularExpressionMatch& m) {
    if (m.captured(1) == QLatin1String("USER"))
      return m.captured(0);
    return standIn("home", QString());
  });

  text = substitute(text, socket, [&](const QRegularExpressionMatch& m) {
    if (m.captured(2) == QLatin1String("USER"))
      return m.captured(0);
    standIn("home", QString());
    return m.captured(1) + "USER" + m.captured(3);
  });

  text = substitute(text, userdata, [&](const QRegularExpressionMatch& m) {
    return m.captured(1) + standIn("steam", QString());
  });

  text = substitute(text, steamId, [&](const QRegularExpressionMatch&) {
    return standIn("steam", QString());
  });

  text = substitute(text, item, [&](const QRegularExpressionMatch& m) {
    QStringList numbers;
    for (const QString& number : m.captured(2).split(','))
      numbers << standIn("item", number);
    return m.captured(1) + numbers.join(',');
  });

  text = substitute(text, fileName, [&](const QRegularExpressionMatch& m) {
    const QString base = m.captured(1);
    if (base == QLatin1String("file") || base.startsWith(QLatin1String("file-")) ||
        base.startsWith(QLatin1String("title-")))
      return m.captured(0);
    return standIn("file", base.toLower()) + "." + m.captured(2);
  });

  return text;
}

/////////////////////////////////////////////////////////////////////////////////////////
QStringList ReportRedactor::summary() const
{
  QStringList lines;
  for (const KindInfo& info : kKinds)
  {
    const auto found = m_kinds.constFind(QString::fromLatin1(info.kind));
    if (found == m_kinds.constEnd() || found->places == 0)
      continue;

    const QString label = QString::fromLatin1(info.label);
    if (found->standIns.isEmpty())
      lines << QString("%1: replaced in %2 places").arg(label).arg(found->places);
    else
      lines << QString("%1: %2 different, replaced in %3 places")
                 .arg(label).arg(found->standIns.size()).arg(found->places);
  }
  return lines;
}

/////////////////////////////////////////////////////////////////////////////////////////
QString ProblemReport::defaultDirectory()
{
  const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (!downloads.isEmpty() && QDir().mkpath(downloads) && QFileInfo(downloads).isWritable())
    return QDir(downloads).absolutePath();
  return Paths::dataDir("reports");
}

/////////////////////////////////////////////////////////////////////////////////////////
QList<ReportFile> ProblemReport::collect(bool pictures, const QString& summary)
{
  QList<ReportFile> files;
  files << ReportFile{"summary.txt", QString(), summary.toUtf8(), true};

  for (const QString& name : {QString("plexmediaplayer.conf"), QString("gl-state")})
  {
    const QString path = Paths::dataDir(name);
    if (QFile::exists(path))
      files << ReportFile{name, path, readCapped(path, kLogLimit), true};
  }

  QStringList logs = {"plexmediaplayer.log"};
  for (int n = 1; n <= 9; n++)
    logs << QString("plexmediaplayer.log.%1").arg(n);
  logs << "stderr.log";
  for (const QString& name : logs)
  {
    const QString path = Paths::logDir(name);
    if (QFile::exists(path))
      files << ReportFile{"logs/" + name, path, readCapped(path, kLogLimit), true};
  }

  const QDir openvr(Paths::dataDir("openvr/log"));
  for (const QFileInfo& info : openvr.entryInfoList({"*.txt", "*.log"}, QDir::Files))
  {
    files << ReportFile{"openvr/" + info.fileName(), info.absoluteFilePath(),
                        readCapped(info.absoluteFilePath(), kLogLimit), true};
  }

  if (pictures)
  {
    const QDir logDir(Paths::logDir());
    const QFileInfoList shots =
      logDir.entryInfoList({"stereo-*.png"}, QDir::Files, QDir::Time);
    for (const QFileInfo& info : shots.mid(0, kPictureLimit))
    {
      files << ReportFile{"pictures/" + info.fileName(), info.absoluteFilePath(),
                          readCapped(info.absoluteFilePath(), 64 * 1024 * 1024), false};
    }
  }

  return files;
}

/////////////////////////////////////////////////////////////////////////////////////////
QString ProblemReport::write(const QList<ReportFile>& files, const QString& directory,
                             QString* error)
{
  ReportRedactor redactor;
  QList<QString> texts;
  for (const ReportFile& file : files)
  {
    texts << (file.redact ? QString::fromUtf8(file.data) : QString());
    if (file.redact)
      redactor.learn(texts.last());
  }

  const QDateTime now = QDateTime::currentDateTime();
  const QString stem = "plex-media-player-report-" + now.toString("yyyyMMdd-HHmmss");
  if (!QDir().mkpath(directory))
  {
    if (error)
      *error = QString("cannot create %1").arg(directory);
    return QString();
  }

  const QString path = QDir(directory).filePath(stem + ".tar.gz");
  TarGzWriter writer(path);
  if (!writer.ok())
  {
    if (error)
      *error = writer.error();
    return QString();
  }

  const qint64 mtime = now.toSecsSinceEpoch();
  QStringList listing;
  for (int n = 0; n < files.size(); n++)
  {
    const ReportFile& file = files[n];
    if (file.redact)
    {
      const QByteArray data = redactor.redact(texts[n]).toUtf8();
      writer.add(stem + "/" + file.name, data, mtime);
      listing << QString("%1 (%2 bytes)").arg(file.name).arg(data.size());
    }
    else
    {
      writer.add(stem + "/" + file.name, file.data, mtime);
      listing << QString("%1 (%2 bytes, not changed)").arg(file.name).arg(file.data.size());
    }
  }

  QStringList replaced = redactor.summary();
  if (replaced.isEmpty())
    replaced << "nothing";

  QString notes;
  notes += "Replaced in this report\n\n";
  for (const QString& line : replaced)
    notes += "  " + line + "\n";
  notes += "\nEach stand-in, such as server-1, means the same thing everywhere in this report.\n"
           "The original values are not in it. Plex logs some free text that cannot be\n"
           "recognised, so read the files before you share them.\n\nFiles\n\n";
  for (const QString& line : listing)
    notes += "  " + line + "\n";
  writer.add(stem + "/replaced.txt", notes.toUtf8(), mtime);

  if (!writer.finish())
  {
    if (error)
      *error = writer.error();
    return QString();
  }

  return path;
}
