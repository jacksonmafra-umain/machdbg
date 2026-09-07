// This file implements accessibility interface for RegistersView
#ifndef QT_NO_ACCESSIBILITY
#include "AccessibleRegistersView.h"
#include "StringUtil.h"

static QRect widgetGlobalRect(const QWidget* widget)
{
    if(!widget)
        return QRect();
    return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size());
}

static QRect registersViewportGlobalRect(const RegistersView* view)
{
    return widgetGlobalRect(view ? view->viewport() : nullptr);
}

static bool isFullLineRegister(RegistersView::REGISTER_NAME reg)
{
    return (reg >= RegistersView::CAX && reg <= RegistersView::EFLAGS)
           || (reg >= RegistersView::MM0 && reg <= RegistersView::MM7)
           || (reg >= RegistersView::DR0 && reg <= RegistersView::DR7)
           || (reg >= RegistersView::K0 && reg <= RegistersView::K7)
           || (reg >= RegistersView::XMM0 && reg <= ArchValue(RegistersView::XMM7, RegistersView::XMM31));
}

AccessibleRegistersViewItem::AccessibleRegistersViewItem(AccessibleRegistersView* parent, RegistersView::REGISTER_NAME id)
    : id(id)
    , mParent(parent)
{
}

QString AccessibleRegistersViewItem::text(QAccessible::Text t) const
{
    if(!isValid())
        return QString();

    RegistersView* view = mParent->m_registersView;
    switch(t)
    {
    case QAccessible::Name:
    case QAccessible::Value:
    {
        const auto it = view->mRegisterMapping.constFind(id);
        if(it == view->mRegisterMapping.cend())
            return QString();
        if(view->mLABELDISPLAY.contains(id))
            return QString(it.value()) + " = " + view->GetRegStringValueFromValue(id, view->registerValue(&view->mRegDumpStruct, id)) + ' ' + view->getRegisterLabel(id);
        return QString(it.value()) + " = " + view->GetRegStringValueFromValue(id, view->registerValue(&view->mRegDumpStruct, id));
    }
    case QAccessible::Help:
        return view->helpRegister(id);
    default:
        return QString();
    }
}

QColor AccessibleRegistersViewItem::foregroundColor() const
{
    if(!isValid())
        return QColor();
    if(mParent->m_registersView->mRegisterUpdates.contains(id))
        return ConfigColor("RegistersModifiedColor");
    return ConfigColor("RegistersColor");
}

int AccessibleRegistersViewItem::childCount() const
{
    return 0;
}

QWindow* AccessibleRegistersViewItem::window() const
{
    return mParent ? mParent->window() : nullptr;
}

QAccessibleInterface* AccessibleRegistersViewItem::parent() const
{
    return mParent;
}

QAccessibleInterface* AccessibleRegistersViewItem::child(int index) const
{
    Q_UNUSED(index);
    return nullptr;
}

int AccessibleRegistersViewItem::indexOfChild(const QAccessibleInterface* child) const
{
    Q_UNUSED(child);
    return -1;
}

QAccessible::Role AccessibleRegistersViewItem::role() const
{
    return QAccessible::ListItem;
}

QAccessible::State AccessibleRegistersViewItem::state() const
{
    QAccessible::State result;
    if(!isValid())
    {
        result.invalid = true;
        return result;
    }

    const RegistersView* view = mParent->m_registersView;
    const bool offscreen = rect().isEmpty();
    const bool enabled = view->isEnabled() && view->isActive;
    result.disabled = !enabled;
    result.focusable = enabled;
    result.selectable = enabled;
    // Scrolled-out registers are still valid list items. On Windows, marking
    // them invisible removes them from UIA sibling navigation; offscreen keeps
    // them in the tree while accurately reporting their clipped geometry.
    result.invisible = !view->isVisible();
    result.offscreen = offscreen;
    result.readOnly = true;
    if(view->mSelected == id)
    {
        result.selected = true;
        result.focused = view->hasFocus();
    }
    return result;
}

QAccessibleInterface* AccessibleRegistersViewItem::childAt(int x, int y) const
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    return nullptr;
}

QObject* AccessibleRegistersViewItem::object() const
{
    return nullptr;
}

void AccessibleRegistersViewItem::setText(QAccessible::Text t, const QString & text)
{
    Q_UNUSED(t);
    Q_UNUSED(text);
}

QRect AccessibleRegistersViewItem::rect() const
{
    if(!isValid())
        return QRect();

    const RegistersView* view = mParent->m_registersView;
    const QWidget* contentWidget = view->widget();
    const auto it = view->mRegisterPlaces.constFind(id);
    if(!contentWidget || it == view->mRegisterPlaces.cend())
        return QRect();

    int ySpace = view->yTopSpacing;
    if(view->mVScrollOffset != 0)
        ySpace = 0;
    const int top = view->mRowHeight * (it.value().line + view->mVScrollOffset) + ySpace;

    int left = 0;
    int right = 0;
    if(isFullLineRegister(it.key()))
    {
        right = contentWidget->width();
    }
    else
    {
        left = (1 + it.value().start) * view->mCharWidth;
        right = left + ((it.value().labelwidth + it.value().valuesize) * view->mCharWidth);
    }

    QRect itemRect(QPoint(left, top), QSize(right - left, view->mRowHeight));
    QRect globalRect(contentWidget->mapToGlobal(itemRect.topLeft()), itemRect.size());
    return globalRect.intersected(registersViewportGlobalRect(view));
}

bool AccessibleRegistersViewItem::isValid() const
{
    if(!mParent || !mParent->isValid())
        return false;
    const int registerIndex = static_cast<int>(id);
    const RegistersView* view = mParent->m_registersView;
    return registerIndex >= 0
           && registerIndex < static_cast<int>(RegistersView::UNKNOWN)
           && view->mRegisterPlaces.contains(id)
           && view->mRegisterMapping.contains(id);
}

AccessibleRegistersView::AccessibleRegistersView(QWidget* w)
    : QAccessibleWidget(w, QAccessible::List, dynamic_cast<RegistersView*>(w)->accessibleName())
    , m_registersView(dynamic_cast<RegistersView*>(w))
{
    assert(m_registersView);
    interfaces.fill(0);
}

AccessibleRegistersView::~AccessibleRegistersView()
{
    for(const auto id : interfaces)
    {
        if(id != 0)
            QAccessible::deleteAccessibleInterface(id);
    }
}

std::vector<RegistersView::REGISTER_NAME> AccessibleRegistersView::registerOrder() const
{
    std::vector<RegistersView::REGISTER_NAME> result;
    if(!m_registersView)
        return result;

    result.reserve(m_registersView->mRegisterPlaces.size());
    for(int index = 0; index < static_cast<int>(RegistersView::UNKNOWN); index++)
    {
        const auto reg = static_cast<RegistersView::REGISTER_NAME>(index);
        if(m_registersView->mRegisterPlaces.contains(reg)
                && m_registersView->mRegisterMapping.contains(reg))
            result.push_back(reg);
    }
    return result;
}

QAccessibleInterface* AccessibleRegistersView::interfaceForRegister(RegistersView::REGISTER_NAME reg) const
{
    const int index = static_cast<int>(reg);
    if(index < 0
            || index >= static_cast<int>(RegistersView::UNKNOWN)
            || !m_registersView->mRegisterPlaces.contains(reg)
            || !m_registersView->mRegisterMapping.contains(reg))
        return nullptr;

    auto & accessibleId = interfaces[index];
    if(accessibleId == 0)
    {
        accessibleId = QAccessible::registerAccessibleInterface(
                           new AccessibleRegistersViewItem(const_cast<AccessibleRegistersView*>(this), reg));
    }
    return QAccessible::accessibleInterface(accessibleId);
}

int AccessibleRegistersView::childCount() const
{
    return static_cast<int>(registerOrder().size());
}

QAccessibleInterface* AccessibleRegistersView::child(int index) const
{
    const auto order = registerOrder();
    if(index < 0 || index >= static_cast<int>(order.size()))
        return nullptr;
    return interfaceForRegister(order[index]);
}

QAccessibleInterface* AccessibleRegistersView::childAt(int x, int y) const
{
    const QWidget* contentWidget = m_registersView ? m_registersView->widget() : nullptr;
    if(!m_registersView || !contentWidget)
        return nullptr;

    const QPoint globalPos(x, y);
    if(!registersViewportGlobalRect(m_registersView).contains(globalPos))
        return nullptr;

    RegistersView::REGISTER_NAME clickedReg = RegistersView::UNKNOWN;
    const QPoint local = contentWidget->mapFromGlobal(globalPos);
    if(m_registersView->identifyRegister((local.y() - m_registersView->yTopSpacing) / static_cast<double>(m_registersView->mRowHeight),
                                         local.x() / static_cast<double>(m_registersView->mCharWidth), &clickedReg))
    {
        if(auto item = interfaceForRegister(clickedReg))
            return item->rect().contains(globalPos) ? item : nullptr;
    }
    return nullptr;
}

int AccessibleRegistersView::indexOfChild(const QAccessibleInterface* child) const
{
    if(!child)
        return -1;
    for(int i = 0; i < childCount(); i++)
    {
        if(this->child(i) == child)
            return i;
    }
    return -1;
}

QAccessibleInterface* AccessibleRegistersView::focusChild() const
{
    if(!m_registersView->hasFocus())
        return nullptr;
    if(auto selected = interfaceForRegister(m_registersView->mSelected))
        return selected;
    return const_cast<AccessibleRegistersView*>(this);
}

bool AccessibleRegistersView::isValid() const
{
    return QAccessibleWidget::isValid() && m_registersView;
}

QAccessible::State AccessibleRegistersView::state() const
{
    QAccessible::State result = QAccessibleWidget::state();
    result.disabled = result.disabled || !m_registersView->isActive;
    result.readOnly = true;
    result.multiLine = true;
    result.multiSelectable = false;
    return result;
}

QRect AccessibleRegistersView::rect() const
{
    return QAccessibleWidget::rect();
}
#endif
