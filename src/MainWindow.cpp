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
#include "PortSnifferWidget.h"
#include "ProcessDetailsDialog.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QScrollArea>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  loadCustomPorts();
  setWindowIcon(QIcon(":/icon.png"));

  // Create tray icon FIRST before setupUi because settings loading
  // during UI setup triggers saveSettings -> updateTrayMenu
  createTrayIcon();

  setupUi();

  m_model = new PortTableModel(this);
  m_portTable->setModel(m_model);
  connect(
      m_portTable, &QTableView::clicked, this,
      [this](const QModelIndex &index) {
        if (index.column() == PortTableModel::Action) {
          int port =
              m_model->data(m_model->index(index.row(), PortTableModel::Port))
                  .toInt();
          QString state =
              m_model->data(m_model->index(index.row(), PortTableModel::State))
                  .toString();
          if (state == "LISTEN" && port > 0) {
            QDesktopServices::openUrl(
                QUrl(QString("http://localhost:%1").arg(port)));
          }
        }
      });

  m_portMonitor = new PortMonitor(this);
  connect(m_portMonitor, &PortMonitor::portsUpdated, this,
          &MainWindow::onPortsUpdated);
  connect(m_portMonitor, &PortMonitor::newPortDetected, this,
          [this](const PortInfo &info) {
            if (m_trayIcon && m_trayIcon->isVisible() && m_notificationsCheck &&
                m_notificationsCheck->isChecked()) {
              QString msg = QString("Process: %1\nPort: %2 (%3)")
                                .arg(info.processName)
                                .arg(info.port)
                                .arg(info.protocol);
              m_trayIcon->showMessage("New Listener Detected", msg,
                                      QSystemTrayIcon::Information, 3000);
            }
            // Log to activity tab
            addLogEntry("New Port", info);
          });

  connect(m_portMonitor, &PortMonitor::portClosed, this,
          [this](const PortInfo &info) { addLogEntry("Port Closed", info); });

  m_refreshTimer = new QTimer(this);
  connect(m_refreshTimer, &QTimer::timeout, this,
          &MainWindow::onRefreshClicked);
  m_refreshTimer->start(5000);

  onRefreshClicked();
}

MainWindow::~MainWindow() {}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (m_trayIcon->isVisible()) {
    QMessageBox::information(this, "Port Monitor",
                             "The application will keep running in the system "
                             "tray. To terminate the program, choose 'Quit' in "
                             "the context menu of the system tray entry.");
    hide();
    event->ignore();
  }
}

void MainWindow::createTrayIcon() {
  m_trayMenu = new QMenu(this);
  QAction *restoreAction =
      m_trayMenu->addAction("Restore", this, &QWidget::showNormal);
  QAction *quitAction =
      m_trayMenu->addAction("Quit", qApp, &QApplication::quit);

  m_trayIcon = new QSystemTrayIcon(this);
  m_trayIcon->setContextMenu(m_trayMenu);

  updateTrayMenu(); // Initial population

  // Use the app icon if available, or a standard one
  QIcon icon = QIcon(":/icon.png");
  if (icon.isNull()) {
    icon = QIcon::fromTheme("network-wired"); // Fallback
  }
  m_trayIcon->setIcon(icon);

  connect(m_trayIcon, &QSystemTrayIcon::activated, this,
          &MainWindow::onTrayIconActivated);
  m_trayIcon->show();
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
  if (reason == QSystemTrayIcon::Trigger ||
      reason == QSystemTrayIcon::DoubleClick) {
    if (isVisible()) {
      hide();
    } else {
      showNormal();
      activateWindow();
    }
  }
}

void MainWindow::setupUi() {
  QWidget *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  QVBoxLayout *rootLayout = new QVBoxLayout(centralWidget);
  rootLayout->setContentsMargins(0, 0, 0, 0);

  m_tabWidget = new QTabWidget(this);
  rootLayout->addWidget(m_tabWidget);

  // --- Tab 1: Monitor ---
  QWidget *monitorTab = new QWidget();
  QVBoxLayout *monitorLayout = new QVBoxLayout(monitorTab);
  monitorLayout->setContentsMargins(15, 15, 15, 15);
  monitorLayout->setSpacing(15);

  // --- Dashboard Section ---
  QLabel *dashTitle = new QLabel("Developer Port Dashboard", this);
  dashTitle->setStyleSheet(
      "font-size: 18px; font-weight: bold; color: #3daee9;");
  monitorLayout->addWidget(dashTitle);

  QFrame *dashFrame = new QFrame(this);
  dashFrame->setObjectName("dashFrame");
  dashFrame->setStyleSheet("#dashFrame { background-color: #333333; "
                           "border-radius: 8px; padding: 10px; }");
  m_dashboardLayout = new FlowLayout(dashFrame);
  m_dashboardLayout->setSpacing(10);

  setupDashboard();
  monitorLayout->addWidget(dashFrame);

  // --- Control Section ---
  QHBoxLayout *topLayout = new QHBoxLayout();
  m_searchBox = new QLineEdit(this);
  m_searchBox->setPlaceholderText("Search processes or ports...");
  connect(m_searchBox, &QLineEdit::textChanged, this,
          &MainWindow::onFilterTextChanged);

  m_refreshBtn = new QPushButton("Refresh Now", this);
  m_refreshBtn->setCursor(Qt::PointingHandCursor);
  connect(m_refreshBtn, &QPushButton::clicked, this,
          &MainWindow::onRefreshClicked);

  topLayout->addWidget(m_searchBox, 1);
  topLayout->addWidget(m_refreshBtn);
  // Settings button removed as it is now a tab

  monitorLayout->addLayout(topLayout);

  // --- Table Section ---
  m_portTable = new QTableView(this);
  m_portTable->setAlternatingRowColors(true);
  m_portTable->horizontalHeader()->setStretchLastSection(true);
  m_portTable->verticalHeader()->setVisible(false);
  m_portTable->setShowGrid(false);
  m_portTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_portTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_portTable->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_portTable, &QTableView::customContextMenuRequested, this,
          &MainWindow::onCustomContextMenuRequested);
  connect(m_portTable, &QTableView::doubleClicked, this,
          &MainWindow::showProcessDetails);

  // Set column widths
  m_portTable->horizontalHeader()->setSectionResizeMode(
      PortTableModel::ProcessName, QHeaderView::Stretch);
  m_portTable->horizontalHeader()->setSectionResizeMode(
      PortTableModel::Port, QHeaderView::ResizeToContents);
  m_portTable->horizontalHeader()->setSectionResizeMode(PortTableModel::Action,
                                                        QHeaderView::Fixed);
  m_portTable->horizontalHeader()->resizeSection(PortTableModel::Action, 100);

  monitorLayout->addWidget(m_portTable);

  m_tabWidget->addTab(monitorTab, "Monitor");

  // --- Tab 2: Activity Log ---
  QWidget *logTab = new QWidget();
  QVBoxLayout *logLayout = new QVBoxLayout(logTab);
  logLayout->setContentsMargins(15, 15, 15, 15);
  logLayout->setSpacing(10);

  // Log Toolbar
  QHBoxLayout *logToolLayout = new QHBoxLayout();

  m_logSearchBox = new QLineEdit(this);
  m_logSearchBox->setPlaceholderText("Search logs...");
  connect(m_logSearchBox, &QLineEdit::textChanged, this,
          &MainWindow::filterActivityLog);

  m_logFilterCombo = new QComboBox(this);
  m_logFilterCombo->addItems(
      {"All Fields", "Time", "Event", "Process", "Port", "User"});
  connect(m_logFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &MainWindow::filterActivityLog);

  logToolLayout->addWidget(m_logSearchBox, 1);
  logToolLayout->addWidget(m_logFilterCombo);

  QPushButton *clearLogBtn = new QPushButton("Clear Logs", this);
  connect(clearLogBtn, &QPushButton::clicked, this,
          [this]() { m_logTable->setRowCount(0); });
  logToolLayout->addWidget(clearLogBtn);

  logLayout->addLayout(logToolLayout);

  m_logTable = new QTableWidget(0, 5, this);
  QStringList logHeaders = {"Time", "Event", "Process", "Port", "User"};
  m_logTable->setHorizontalHeaderLabels(logHeaders);
  m_logTable->horizontalHeader()->setStretchLastSection(true);
  m_logTable->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_logTable->setAlternatingRowColors(true);

  logLayout->addWidget(m_logTable);

  m_tabWidget->addTab(logTab, "Activity Log");

  // --- Tab 3: Detailed Monitor ---
  PortSnifferWidget *snifferTab = new PortSnifferWidget(this);
  m_tabWidget->addTab(snifferTab, "Deep Monitor");

  // --- Tab 4: Settings ---
  setupSettingsTab(m_tabWidget);

  statusBar()->showMessage("System Ready");
}

void MainWindow::addLogEntry(const QString &event, const PortInfo &info) {
  int row = 0;
  m_logTable->insertRow(row);

  QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");

  m_logTable->setItem(row, 0, new QTableWidgetItem(timestamp));
  m_logTable->setItem(row, 1, new QTableWidgetItem(event));
  m_logTable->setItem(row, 2, new QTableWidgetItem(info.processName));
  m_logTable->setItem(row, 3, new QTableWidgetItem(QString::number(info.port)));
  m_logTable->setItem(row, 4, new QTableWidgetItem(info.user));

  // Ensure the new top item is visible
  m_logTable->scrollToTop();
}

void MainWindow::setupDashboard() {
  // Clear existing layout items
  QLayoutItem *child;
  while ((child = m_dashboardLayout->takeAt(0)) != nullptr) {
    if (child->widget()) {
      child->widget()->deleteLater();
    }
    delete child;
  }
  m_trackedPorts.clear();

  // 1. Default Ports — EMPTY (user can add their own)
  QList<PortDef> defs;

  // 2. Append Custom Ports
  defs.append(m_customPorts);

  int col = 0;
  int row = 0;
  QWidget *parentWidget = m_dashboardLayout->parentWidget();
  int defaultPortCount = defs.size() - m_customPorts.size();

  for (int i = 0; i < defs.size(); ++i) {
    const auto &def = defs[i];
    bool isCustom = (i >= defaultPortCount);

    QWidget *container = new QWidget(parentWidget);
    container->setProperty("class", "dashboardCard");
    container->setFixedSize(160, 110);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(5);

    QLabel *nameLabel =
        new QLabel(QString("%1 (%2)").arg(def.name).arg(def.port), container);
    nameLabel->setObjectName("dashPortName");
    nameLabel->setWordWrap(true);

    QLabel *statusLabel = new QLabel("OFFLINE", container);
    statusLabel->setObjectName("dashStatusLabel");
    statusLabel->setStyleSheet("color: #888888;");
    statusLabel->setToolTip(def.desc);

    QPushButton *openBtn = new QPushButton("Launch", container);
    openBtn->setObjectName("dashOpenBtn");
    openBtn->setCursor(Qt::PointingHandCursor);
    openBtn->setVisible(false);
    connect(openBtn, &QPushButton::clicked, this, [this, def]() {
      QDesktopServices::openUrl(
          QUrl(QString("http://localhost:%1").arg(def.port)));
    });

    // Add delete button for custom ports (positioned in top-right corner)
    QPushButton *deleteBtn = nullptr;
    if (isCustom) {
      deleteBtn = new QPushButton(container);
      deleteBtn->setObjectName("dashDeleteBtn");
      deleteBtn->setFixedSize(24, 24);
      deleteBtn->setStyleSheet(
          "QPushButton { background-color: transparent; border: none; }"
          "QPushButton:hover { background-color: rgba(231, 76, 60, 0.2); "
          "border-radius: 12px; }");
      deleteBtn->setCursor(Qt::PointingHandCursor);
      deleteBtn->setToolTip("Remove this custom port");

      // Create red trash icon programmatically
      QPixmap trashIcon(24, 24);
      trashIcon.fill(Qt::transparent);
      QPainter painter(&trashIcon);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setPen(QPen(QColor("#e74c3c"), 2.0));
      painter.setBrush(Qt::NoBrush);

      // Draw trash can body
      painter.drawRect(7, 10, 10, 10);
      // Draw trash can lid
      painter.drawLine(6, 9, 18, 9);
      painter.drawLine(8, 7, 16, 7);
      // Draw vertical lines inside
      painter.drawLine(10, 12, 10, 18);
      painter.drawLine(14, 12, 14, 18);

      painter.end();
      deleteBtn->setIcon(QIcon(trashIcon));

      // Position in top-right corner using absolute positioning
      deleteBtn->move(container->width() - 28, 4);
      deleteBtn->raise(); // Ensure it's on top

      int portToDelete = def.port;
      connect(deleteBtn, &QPushButton::clicked, this, [this, portToDelete]() {
        // Remove from custom ports list
        for (int j = 0; j < m_customPorts.size(); ++j) {
          if (m_customPorts[j].port == portToDelete) {
            m_customPorts.removeAt(j);
            break;
          }
        }
        saveCustomPorts();
        setupDashboard();
        onRefreshClicked();
      });
    }

    layout->addWidget(nameLabel);
    layout->addWidget(statusLabel);
    layout->addStretch();
    layout->addWidget(openBtn);

    m_dashboardLayout->addWidget(container);
    container->show();

    m_trackedPorts.append({def.port, def.name, def.desc, statusLabel, container,
                           openBtn, deleteBtn, isCustom});
  }

  // 3. Add "Add New Port" Card
  QPushButton *addPortBtn = new QPushButton(parentWidget);
  addPortBtn->setFixedSize(160, 110);
  addPortBtn->setStyleSheet("QPushButton { "
                            "  background-color: transparent; "
                            "  border: 2px dashed #555555; "
                            "  border-radius: 8px; "
                            "  color: #888888; "
                            "  font-weight: bold; "
                            "  font-size: 14px; "
                            "}"
                            "QPushButton:hover { "
                            "  border-color: #3daee9; "
                            "  color: #3daee9; "
                            "  background-color: rgba(61, 174, 233, 0.1);"
                            "}");
  addPortBtn->setText("+ Add Port");
  addPortBtn->setCursor(Qt::PointingHandCursor);

  connect(addPortBtn, &QPushButton::clicked, this,
          &MainWindow::onAddPortClicked);

  m_dashboardLayout->addWidget(addPortBtn);
  addPortBtn->show();

  // Force layout update
  m_dashboardLayout->update();
  m_dashboardLayout->activate();
}

void MainWindow::onAddPortClicked() {
  QDialog dialog(this);
  dialog.setWindowTitle("Add New Port");
  dialog.setModal(true);
  dialog.setStyleSheet("background-color: #2b2b2b; color: #ffffff;");

  QFormLayout form(&dialog);

  QLineEdit *nameEdit = new QLineEdit(&dialog);
  nameEdit->setPlaceholderText("e.g. My API Server");
  nameEdit->setStyleSheet(
      "padding: 5px; border: 1px solid #555; border-radius: 4px; background: "
      "#1e1e1e; color: white;");

  QSpinBox *portEdit = new QSpinBox(&dialog);
  portEdit->setRange(1, 65535);
  portEdit->setValue(8080);
  portEdit->setStyleSheet(
      "padding: 5px; border: 1px solid #555; border-radius: 4px; background: "
      "#1e1e1e; color: white;");

  form.addRow("Service Name:", nameEdit);
  form.addRow("Port Number:", portEdit);

  QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             Qt::Horizontal, &dialog);
  buttonBox.setStyleSheet("QPushButton { padding: 5px 15px; }");
  connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  form.addRow(&buttonBox);

  if (dialog.exec() == QDialog::Accepted) {
    QString name = nameEdit->text().trimmed();
    int port = portEdit->value();

    if (name.isEmpty())
      name = "Unknown Service";

    // Add to custom ports
    m_customPorts.append({port, name, "User defined port"});
    saveCustomPorts();

    // Refresh dashboard
    setupDashboard();

    // Trigger a scan immediately so it updates status
    onRefreshClicked();
  }
}

void MainWindow::saveCustomPorts() {
  QSettings settings("KadirMertAbatay", "PortMonitor");
  QStringList list;
  for (const auto &p : m_customPorts) {
    list << QString("%1:%2").arg(p.port).arg(p.name);
  }
  settings.setValue("customPorts", list);
}

void MainWindow::loadCustomPorts() {
  QSettings settings("KadirMertAbatay", "PortMonitor");
  QStringList list = settings.value("customPorts").toStringList();
  m_customPorts.clear();

  for (const QString &item : list) {
    QStringList parts = item.split(":");
    if (parts.size() >= 2) {
      bool ok;
      int port = parts[0].toInt(&ok);
      if (ok) {
        QString name =
            parts.mid(1).join(":"); // Rejoin rest in case name has colons
        m_customPorts.append({port, name, "User defined port"});
      }
    }
  }
}

void MainWindow::updateDashboard(const QList<PortInfo> &ports) {
  qDebug() << "Updating dashboard with" << ports.size() << "ports";
  for (auto &tracked : m_trackedPorts) {
    bool found = false;
    QString process;
    for (const auto &p : ports) {
      if (p.port == tracked.port) {
        qDebug() << "Matched port" << tracked.port << "with process"
                 << p.processName;
        found = true;
        process = p.processName;
        break;
      }
    }

    if (found) {
      tracked.label->setText(QString("● ONLINE (%1)").arg(process));
      tracked.label->setStyleSheet("color: #81c784; font-weight: bold;");
      tracked.openButton->setVisible(true);
      tracked.container->setProperty("online", true);
    } else {
      tracked.label->setText("○ OFFLINE");
      tracked.label->setStyleSheet("color: #666666;");
      tracked.openButton->setVisible(false);
      tracked.container->setProperty("online", false);
    }
    // Refresh style
    tracked.container->style()->unpolish(tracked.container);
    tracked.container->style()->polish(tracked.container);
  }
  updateTrayMenu();
}

void MainWindow::onRefreshClicked() {
  statusBar()->showMessage("Scanning ports...");
  m_portMonitor->refresh();
}

void MainWindow::onPortsUpdated(const QList<PortInfo> &ports) {
  m_allPorts = ports;
  updateDashboard(ports);
  onFilterTextChanged(m_searchBox->text());
  statusBar()->showMessage(QString("Active connections: %1").arg(ports.size()));
}

void MainWindow::onFilterTextChanged(const QString &text) {
  // Filter Dashboard Cards
  for (const auto &tracked : m_trackedPorts) {
    bool match = text.isEmpty() ||
                 tracked.name.contains(text, Qt::CaseInsensitive) ||
                 tracked.description.contains(text, Qt::CaseInsensitive) ||
                 QString::number(tracked.port).contains(text);
    tracked.container->setVisible(match);
  }

  // Filter Table
  if (text.isEmpty()) {
    m_model->setPorts(m_allPorts);
    return;
  }
  QList<PortInfo> filtered;
  for (const PortInfo &info : m_allPorts) {
    if (info.processName.contains(text, Qt::CaseInsensitive) ||
        info.pid.contains(text, Qt::CaseInsensitive) ||
        QString::number(info.port).contains(text) ||
        info.protocol.contains(text, Qt::CaseInsensitive)) {
      filtered.append(info);
    }
  }
  m_model->setPorts(filtered);
}

void MainWindow::onCustomContextMenuRequested(const QPoint &pos) {
  QModelIndex index = m_portTable->indexAt(pos);
  if (!index.isValid())
    return;

  QMenu contextMenu(this);
  contextMenu.addAction(QIcon::fromTheme("application-exit"), "End Process",
                        this, &MainWindow::onKillProcessRequested);
  contextMenu.addAction(
      QIcon::fromTheme("edit-copy"), "Copy PID", this, [this, index]() {
        int row = index.row();
        QString pid =
            m_model->data(m_model->index(row, PortTableModel::PID)).toString();
        QApplication::clipboard()->setText(pid);
      });
  contextMenu.addSeparator();
  contextMenu.addAction(QIcon::fromTheme("dialog-information"), "View Details",
                        this, &MainWindow::showProcessDetails);

  contextMenu.exec(m_portTable->viewport()->mapToGlobal(pos));
}

void MainWindow::onKillProcessRequested() {
  QModelIndexList selection = m_portTable->selectionModel()->selectedRows();
  if (selection.isEmpty())
    return;

  int row = selection.first().row();
  QString pidStr =
      m_model->data(m_model->index(row, PortTableModel::PID)).toString();
  QString name = m_model->data(m_model->index(row, PortTableModel::ProcessName))
                     .toString();
  bool ok;
  qint64 pid = pidStr.toLongLong(&ok);

  if (ok) {
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(
        this, "End Process",
        QString("Are you sure you want to end process '%1' (PID: %2)?")
            .arg(name)
            .arg(pid),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
      connect(m_portMonitor, &PortMonitor::processKilled, this,
              [this, name, pid](qint64, bool success, const QString &msg) {
                if (!success) {
                  QMessageBox::critical(this, "Error",
                                        "Failed to kill process: " + msg);
                } else {
                  statusBar()->showMessage("Process killed successfully.");

                  // Manually create info for log since port closed might come
                  // later or not carry same details immediately
                  PortInfo info;
                  info.processName = name;
                  info.pid = QString::number(pid);
                  addLogEntry("Process Killed", info);

                  onRefreshClicked();
                }
                // Disconnect to avoid multiple slots overlapping on
                // subsequent calls
                disconnect(m_portMonitor, &PortMonitor::processKilled, this,
                           nullptr);
              });
      m_portMonitor->killProcess(pid);
    }
  }
}

void MainWindow::showProcessDetails() {
  QModelIndexList selection = m_portTable->selectionModel()->selectedRows();
  if (selection.isEmpty())
    return;

  int row = selection.first().row();

  PortInfo info;
  info.processName =
      m_model->data(m_model->index(row, PortTableModel::ProcessName))
          .toString();
  info.pid = m_model->data(m_model->index(row, PortTableModel::PID)).toString();
  info.user =
      m_model->data(m_model->index(row, PortTableModel::User)).toString();
  info.protocol =
      m_model->data(m_model->index(row, PortTableModel::Protocol)).toString();
  info.localAddress =
      m_model->data(m_model->index(row, PortTableModel::LocalAddress))
          .toString();
  info.port = m_model->data(m_model->index(row, PortTableModel::Port)).toInt();
  info.state =
      m_model->data(m_model->index(row, PortTableModel::State)).toString();

  ProcessDetailsDialog dialog(info, this);
  dialog.exec();
}

void MainWindow::setupSettingsTab(QWidget *parent) {
  QWidget *settingsTab = new QWidget();
  QVBoxLayout *mainLayout = new QVBoxLayout(settingsTab);
  mainLayout->setSpacing(0);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setAlignment(Qt::AlignTop);

  // Scroll Area for settings
  QScrollArea *scrollArea = new QScrollArea(settingsTab);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setStyleSheet("background: transparent;");

  QWidget *scrollContent = new QWidget();
  scrollContent->setStyleSheet("background: transparent;");
  QVBoxLayout *layout = new QVBoxLayout(scrollContent);
  layout->setSpacing(20);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setAlignment(Qt::AlignTop);

  // --- Group 1: Notifications ---
  QFrame *notifGroup = new QFrame();
  notifGroup->setProperty("class", "settingsGroup");
  QVBoxLayout *notifLayout = new QVBoxLayout(notifGroup);

  QLabel *notifHeader = new QLabel("Notifications");
  notifHeader->setProperty("class", "settingsGroupHeader");
  notifLayout->addWidget(notifHeader);

  m_notificationsCheck = new QCheckBox("Enable Desktop Notifications");
  m_notificationsCheck->setCursor(Qt::PointingHandCursor);
  connect(m_notificationsCheck, &QCheckBox::checkStateChanged, this,
          &MainWindow::saveSettings);
  notifLayout->addWidget(m_notificationsCheck);

  QLabel *notifDesc = new QLabel(
      "Show system notifications when a new developer port starts listening.");
  notifDesc->setProperty("class", "settingsDesc");
  notifDesc->setWordWrap(true);
  notifLayout->addWidget(notifDesc);

  layout->addWidget(notifGroup);

  // --- Group 3: System ---
  QFrame *systemGroup = new QFrame();
  systemGroup->setProperty("class", "settingsGroup");
  QVBoxLayout *systemLayout = new QVBoxLayout(systemGroup);

  QLabel *systemHeader = new QLabel("System");
  systemHeader->setProperty("class", "settingsGroupHeader");
  systemLayout->addWidget(systemHeader);

  m_autoStartCheck = new QCheckBox("Launch on Startup");
  m_autoStartCheck->setCursor(Qt::PointingHandCursor);
  connect(m_autoStartCheck, &QCheckBox::checkStateChanged, this,
          &MainWindow::saveSettings);
  systemLayout->addWidget(m_autoStartCheck);

  QLabel *systemDesc = new QLabel(
      "Automatically start Port Monitor when you log into your computer.");
  systemDesc->setProperty("class", "settingsDesc");
  systemDesc->setWordWrap(true);
  systemLayout->addWidget(systemDesc);

  layout->addWidget(systemGroup);

  // --- Footer ---
  layout->addStretch();
  QLabel *infoLabel = new QLabel("Settings are saved automatically.");
  infoLabel->setStyleSheet(
      "color: #666666; font-style: italic; font-size: 11px; "
      "margin-top: 10px;");
  infoLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(infoLabel);

  scrollArea->setWidget(scrollContent);
  mainLayout->addWidget(scrollArea);

  if (auto castedParent = qobject_cast<QTabWidget *>(parent)) {
    castedParent->addTab(settingsTab, "Settings");
  }

  loadSettings();
}

void MainWindow::loadSettings() {
  QSettings settings("KadirMertAbatay", "PortMonitor");
  m_notificationsCheck->setChecked(
      settings.value("notifications", true).toBool());

  // Check if plist exists for auto-start
  QString plistPath =
      QDir::homePath() +
      "/Library/LaunchAgents/com.kadirmertabatay.portmonitor.plist";
  m_autoStartCheck->setChecked(QFile::exists(plistPath));
}

void MainWindow::saveSettings() {
  QSettings settings("KadirMertAbatay", "PortMonitor");
  settings.setValue("notifications", m_notificationsCheck->isChecked());

  // Auto-start logic
  QString plistPath =
      QDir::homePath() +
      "/Library/LaunchAgents/com.kadirmertabatay.portmonitor.plist";
  if (m_autoStartCheck->isChecked()) {
    if (!QFile::exists(plistPath)) {
      QFile file(plistPath);
      if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        QString appPath = QApplication::applicationFilePath();
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
               "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            << "<plist version=\"1.0\">\n"
            << "<dict>\n"
            << "    <key>Label</key>\n"
            << "    <string>com.kadirmertabatay.portmonitor</string>\n"
            << "    <key>ProgramArguments</key>\n"
            << "    <array>\n"
            << "        <string>" << appPath << "</string>\n"
            << "    </array>\n"
            << "    <key>RunAtLoad</key>\n"
            << "    <true/>\n"
            << "</dict>\n"
            << "</plist>\n";
        file.close();
      }
    }
  } else {
    if (QFile::exists(plistPath)) {
      QFile::remove(plistPath);
    }
  }

  statusBar()->showMessage("Settings saved.", 2000);
  // Update Tray Menu as well
  updateTrayMenu();
}

bool MainWindow::isDarkTheme() {
  QSettings settings("KadirMertAbatay", "PortMonitor");
  return settings.value("theme", "dark").toString() == "dark";
}

void MainWindow::filterActivityLog() {
  QString text = m_logSearchBox->text().trimmed();
  int filterIndex =
      m_logFilterCombo->currentIndex(); // 0: All, 1: Time, 2: Event, ...

  for (int i = 0; i < m_logTable->rowCount(); ++i) {
    if (text.isEmpty()) {
      m_logTable->setRowHidden(i, false);
      continue;
    }

    bool match = false;
    if (filterIndex == 0) {
      // Check all columns
      for (int j = 0; j < m_logTable->columnCount(); ++j) {
        if (m_logTable->item(i, j)->text().contains(text,
                                                    Qt::CaseInsensitive)) {
          match = true;
          break;
        }
      }
    } else {
      // Check specific column (Index maps 1->0, 2->1 etc if we align headers)
      // Headers: Time(0), Event(1), Process(2), Port(3), User(4)
      // Combo: All(0), Time(1), Event(2), Process(3), Port(4), User(5)
      int col = filterIndex - 1;
      if (col >= 0 && col < m_logTable->columnCount()) {
        if (m_logTable->item(i, col)->text().contains(text,
                                                      Qt::CaseInsensitive)) {
          match = true;
        }
      }
    }
    m_logTable->setRowHidden(i, !match);
  }
}

void MainWindow::updateTrayMenu() {
  if (!m_trayMenu)
    return;

  m_trayMenu->clear();

  // 1. Add Developer Ports Status
  QAction *headerAction = m_trayMenu->addAction("ACTIVE PORTS");
  headerAction->setEnabled(
      false); // Functions as a visual header in native menus

  bool anyActive = false;
  for (const auto &tracked : m_trackedPorts) {
    bool isActive = tracked.container->property("online").toBool();

    if (isActive) {
      anyActive = true;
      // Use Unicode circle for reliable "green icon" visual
      QString text = QString("🟢 %1 (%2)").arg(tracked.name).arg(tracked.port);
      QAction *action = m_trayMenu->addAction(text);

      // Still attempt to set a proper QIcon for consistency, using larger size
      // for Retina
      QPixmap pixmap(32, 32);
      pixmap.fill(Qt::transparent);
      QPainter painter(&pixmap);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setBrush(QColor("#2ecc71"));
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(4, 4, 24, 24);
      painter.end();
      action->setIcon(QIcon(pixmap));

      // Make clickable to open localhost
      int port = tracked.port;
      connect(action, &QAction::triggered, this, [port]() {
        QDesktopServices::openUrl(
            QUrl(QString("http://localhost:%1").arg(port)));
      });
    }
  }

  if (!anyActive) {
    QAction *empty = m_trayMenu->addAction("No active developer ports");
    empty->setEnabled(false);
  }

  m_trayMenu->addSeparator();

  // 2. Standard Actions
  m_trayMenu->addAction("Restore", this, &QWidget::showNormal);
  m_trayMenu->addAction("Quit", qApp, &QApplication::quit);
}
