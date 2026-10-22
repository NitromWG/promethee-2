// Prométhée : export du dossier de fabrication, au choix des formats. Licence GPL-3.0-only.
#pragma once

#include <map>

#include <QDialog>

class Document;
class QCheckBox;
class QLineEdit;

class DialogueExport : public QDialog {
  Q_OBJECT
public:
  DialogueExport(Document* doc, QWidget* parent = nullptr);
  // Écrit les fichiers choisis et renvoie leur liste (lève std::runtime_error en cas d'échec).
  QStringList exporter();

private:
  Document* m_doc;
  QLineEdit* m_dossier;
  std::map<QString, QCheckBox*> m_formats;
};
