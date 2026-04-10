/*
 * Copyright 2025 Kadir Mert Abatay
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "MainWindow.h"
#include <QApplication>
#include <QFile>
#include <QIcon>

int main(int argc, char *argv[]) {
  // Ensure we can see the tray icon on some systems
  QApplication::setQuitOnLastWindowClosed(false);

  QApplication app(argc, argv);
  app.setApplicationName("Port Monitor");
  app.setOrganizationName("KadirMertAbatay");
  app.setWindowIcon(QIcon(":/icon.png"));

  // Load Stylesheet
  QFile styleFile(":/styles.qss");
  if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    app.setStyleSheet(styleFile.readAll());
  }

  MainWindow window;
  window.resize(1000, 700);
  window.show();

  return app.exec();
}
