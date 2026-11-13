// Fase 1, stap 1: kale Qt Widgets-app voor iOS.
// Doel: aantonen dat Qt 6.12 + Xcode 26 + de iPad-simulator samenwerken, los van wxWidgets.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
#include <QDebug>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  QWidget window;
  auto *layout = new QVBoxLayout(&window);
  auto *label = new QLabel("Qt-rooktest: nog niet getikt", &window);
  auto *button = new QPushButton("Tik hier", &window);
  // Vergelijking met wxTextCtrl: selecteren met lang drukken (8 nov 2026).
  auto *edit = new QLineEdit(&window);
  edit->setPlaceholderText("Kaal Qt-invoerveld");
  layout->addWidget(edit);
  layout->addWidget(label);
  layout->addWidget(button);

  int taps = 0;
  QObject::connect(button, &QPushButton::clicked, [&]() {
    ++taps;
    label->setText(QString("Getikt: %1 keer").arg(taps));
    qInfo() << "QTHELLO tik" << taps;
  });

  window.showMaximized();
  qInfo() << "QTHELLO gestart, Qt" << qVersion();
  return app.exec();
}
