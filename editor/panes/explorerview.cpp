#include "explorerview.h"
#include "common.h"
#include "../ui_mainwindow.h"
#include "mainwindow.h"
#include "objects/base/instance.h"
#include "objects/meta.h"
#include "objects/script.h"
#include "objects/service/selection.h"
#include "undohistory.h"
#include <memory>
#include <qaction.h>
#include <qtreeview.h>

#define M_mainWindow dynamic_cast<MainWindow*>(window())

ExplorerView::ExplorerView(QWidget* parent):
    QTreeView(parent),
    model(ExplorerModel(std::dynamic_pointer_cast<Instance>(gDataModel), this)),
    contextMenu(this) {

    this->setModel(&model);
    // Disabling the root decoration will cause the expand/collapse chevrons to be hidden too, we don't want that
    // https://stackoverflow.com/a/4687016/16255372
    // this->setRootIsDecorated(false);
    // The branches can be customized like this if you want:
    // this->setStyleSheet(QString("QTreeView::branch { border: none; }"));
    this->setDragDropMode(QAbstractItemView::InternalMove);
    this->setSelectionMode(QAbstractItemView::ExtendedSelection);
    this->setDragEnabled(true);
    this->setAcceptDrops(true);
    this->setDropIndicatorShown(true);
    this->setContextMenuPolicy(Qt::CustomContextMenu);

    // Expand workspace
    this->expand(model.ObjectToIndex(gWorkspace()));

    connect(this, &QTreeView::customContextMenuRequested, this, [&](const QPoint& point) {
        contextMenu.exec(this->viewport()->mapToGlobal(point));
    });

    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [&](const QItemSelection &selected, const QItemSelection &deselected) {
        // Simple, good old debounce.
        if (handlingSelectionUpdate)
            return;

        std::vector<std::shared_ptr<Instance>> selectedInstances;
        selectedInstances.reserve(selectedIndexes().count()); // This doesn't reserve everything, but should enhance things anyway

        for (auto index : selectedIndexes()) {
            selectedInstances.push_back(reinterpret_cast<Instance*>(index.internalPointer())->shared_from_this());
        }

        std::shared_ptr<Selection> selection = gDataModel->GetService<Selection>();
        selection->Set(selectedInstances);
    });

}

void ExplorerView::setSelectedObjects(std::vector<std::shared_ptr<Instance>> selection) {
    handlingSelectionUpdate = true;

    this->clearSelection();
    for (std::weak_ptr<Instance> inst : selection) {
        if (inst.expired()) continue;
        QModelIndex index = this->model.ObjectToIndex(inst.lock());
        this->selectionModel()->select(index, QItemSelectionModel::SelectionFlag::Select);
    }

    handlingSelectionUpdate = false;
}

ExplorerView::~ExplorerView() {
}

void ExplorerView::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Delete:
        M_mainWindow->ui->actionDelete->trigger();
        break;
    }
}

void ExplorerView::mouseDoubleClickEvent(QMouseEvent *event) {
    QModelIndex index = indexAt(event->pos());
    std::shared_ptr<Instance> inst = model.fromIndex(index);
    if (!inst->IsA<Script>()) return QTreeView::mouseDoubleClickEvent(event);

    MainWindow* mainWnd = dynamic_cast<MainWindow*>(window());
    mainWnd->openScriptDocument(inst->CastTo<Script>().expect());
    QTreeView::mouseDoubleClickEvent(event);
}

void ExplorerView::buildContextMenu() {
    contextMenu.addAction(M_mainWindow->ui->actionDelete);
    contextMenu.addSeparator();
    contextMenu.addAction(M_mainWindow->ui->actionCopy);
    contextMenu.addAction(M_mainWindow->ui->actionCut);
    contextMenu.addAction(M_mainWindow->ui->actionPaste);
    contextMenu.addAction(M_mainWindow->ui->actionPasteInto);
    contextMenu.addSeparator();
    contextMenu.addAction(M_mainWindow->ui->actionSaveModel);
    contextMenu.addAction(M_mainWindow->ui->actionInsertModel);

    // Insert Object menu

    QMenu* insertObjectMenu = new QMenu("Insert Object");
    contextMenu.addMenu(insertObjectMenu);

    for (const auto& [_, type] : INSTANCE_MAP) {
        if (type->flags & (INSTANCE_NOTCREATABLE | INSTANCE_HIDDEN) || !type->constructor) continue;

        QAction* instAction = new QAction(model.iconOf(type), QString::fromStdString(type->className));
        insertObjectMenu->addAction(instAction);
        connect(instAction, &QAction::triggered, this, [&]() {
            std::shared_ptr<Selection> selection = gDataModel->GetService<Selection>();
            if (selection->Get().size() == 0) return;
            std::shared_ptr<Instance> instParent = selection->Get()[0];
            std::shared_ptr<Instance> newInst = type->constructor();
            newInst->SetParent(instParent);
            M_mainWindow->undoManager.PushState({ UndoStateInstanceCreated { newInst, instParent } });
        });
    }
}

void ExplorerView::updateRoot(std::shared_ptr<Instance> newRoot) {
    model.updateRoot(newRoot);
}