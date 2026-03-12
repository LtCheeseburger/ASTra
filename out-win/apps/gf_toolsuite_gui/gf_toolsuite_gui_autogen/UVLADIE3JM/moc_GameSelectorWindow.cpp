/****************************************************************************
** Meta object code from reading C++ file 'GameSelectorWindow.hpp'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../apps/gf_toolsuite_gui/src/GameSelectorWindow.hpp"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'GameSelectorWindow.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN2gf3gui18GameSelectorWindowE_t {};
} // unnamed namespace

template <> constexpr inline auto gf::gui::GameSelectorWindow::qt_create_metaobjectdata<qt_meta_tag_ZN2gf3gui18GameSelectorWindowE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "gf::gui::GameSelectorWindow",
        "openGameRequested",
        "",
        "gameId",
        "openAstEditorRequested",
        "refresh",
        "onAddGame",
        "onRemoveGame",
        "onOpenGameFiles",
        "onOpenAstEditor",
        "onSelectionChanged",
        "onItemDoubleClicked",
        "QListWidgetItem*",
        "item",
        "onAbout",
        "onBetaTestingGuide"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'openGameRequested'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'openAstEditorRequested'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'refresh'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAddGame'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRemoveGame'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onOpenGameFiles'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onOpenAstEditor'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSelectionChanged'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onItemDoubleClicked'
        QtMocHelpers::SlotData<void(QListWidgetItem *)>(11, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 12, 13 },
        }}),
        // Slot 'onAbout'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onBetaTestingGuide'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<GameSelectorWindow, qt_meta_tag_ZN2gf3gui18GameSelectorWindowE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject gf::gui::GameSelectorWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<::QMainWindow::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2gf3gui18GameSelectorWindowE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2gf3gui18GameSelectorWindowE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN2gf3gui18GameSelectorWindowE_t>.metaTypes,
    nullptr
} };

void gf::gui::GameSelectorWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<GameSelectorWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->openGameRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->openAstEditorRequested(); break;
        case 2: _t->refresh(); break;
        case 3: _t->onAddGame(); break;
        case 4: _t->onRemoveGame(); break;
        case 5: _t->onOpenGameFiles(); break;
        case 6: _t->onOpenAstEditor(); break;
        case 7: _t->onSelectionChanged(); break;
        case 8: _t->onItemDoubleClicked((*reinterpret_cast<std::add_pointer_t<QListWidgetItem*>>(_a[1]))); break;
        case 9: _t->onAbout(); break;
        case 10: _t->onBetaTestingGuide(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (GameSelectorWindow::*)(const QString & )>(_a, &GameSelectorWindow::openGameRequested, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (GameSelectorWindow::*)()>(_a, &GameSelectorWindow::openAstEditorRequested, 1))
            return;
    }
}

const QMetaObject *gf::gui::GameSelectorWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *gf::gui::GameSelectorWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN2gf3gui18GameSelectorWindowE_t>.strings))
        return static_cast<void*>(this);
    return ::QMainWindow::qt_metacast(_clname);
}

int gf::gui::GameSelectorWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = ::QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 11;
    }
    return _id;
}

// SIGNAL 0
void gf::gui::GameSelectorWindow::openGameRequested(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void gf::gui::GameSelectorWindow::openAstEditorRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
