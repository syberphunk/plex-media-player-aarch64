#ifndef VERSION_H
#define VERSION_H

namespace Version
{
  QString GetVersionString();
  QString GetReleaseTag();
  QString GetCanonicalVersionString();
  QString GetBuildDate();
  QString GetBuildTimestamp();
  QString GetWebVersion();
  QString GetQtDepsVersion();
  QString GetDependenciesVersion();
};

#endif // VERSION_H

