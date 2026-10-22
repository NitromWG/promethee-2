// Prométhée : accessibilité du canevas du schéma (lot L2, tâche 2.30). Licence GPL-3.0-only.
#include "AccessibiliteCanevas.hpp"

#include <QAccessible>
#include <QAccessibleWidget>

#include "VueSchema.hpp"

namespace {
class CanevasSchema : public QAccessibleWidget {
 public:
  explicit CanevasSchema(QWidget* w) : QAccessibleWidget(w, QAccessible::Canvas) {}
  QString text(QAccessible::Text t) const override {
    auto* v = static_cast<VueSchema*>(widget());
    switch (t) {
      case QAccessible::Name: return v->accessibleName().isEmpty() ? v->nomAccessible() : v->accessibleName();
      case QAccessible::Description:
      case QAccessible::Value: return v->descriptionTenue();
      default: return QAccessibleWidget::text(t);
    }
  }
};

QAccessibleInterface* fabrique(const QString& classe, QObject* objet) {
  if (classe == QLatin1String("VueSchema") || classe == QLatin1String("VueSchemaElectrique"))
    if (auto* w = qobject_cast<QWidget*>(objet)) return new CanevasSchema(w);
  return nullptr;
}
}  // namespace

namespace accessibilite {
void installer() {
  static bool fait = false;
  if (fait) return;
  fait = true;
  QAccessible::installFactory(fabrique);
}

void annoncer(QWidget* canevas) {
  if (!QAccessible::isActive() || !canevas) return;
  QAccessibleEvent e(canevas, QAccessible::DescriptionChanged);
  QAccessible::updateAccessibility(&e);
  QAccessibleEvent n(canevas, QAccessible::NameChanged);
  QAccessible::updateAccessibility(&n);
}
}  // namespace accessibilite
