/* This file is part of Clementine.
   Copyright 2010, David Sansome <me@davidsansome.com>

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "playlistcontainer.h"
#include "playlistmanager.h"
#include "ui_playlistcontainer.h"
#include "playlistparsers/playlistparser.h"
#include "ui/iconloader.h"
#include "widgets/maclineedit.h"

#include <QUndoStack>
#include <QInputDialog>
#include <QSettings>
#include <QTimeLine>
#include <QSortFilterProxyModel>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>

const char* PlaylistContainer::kSettingsGroup = "Playlist";

PlaylistContainer::PlaylistContainer(QWidget *parent)
  : QWidget(parent),
    ui_(new Ui_PlaylistContainer),
    manager_(NULL),
    undo_(NULL),
    redo_(NULL),
    starting_up_(true),
    tab_bar_visible_(false),
    tab_bar_animation_(new QTimeLine(500, this)),
    no_matches_label_(new QLabel(this))
{
  ui_->setupUi(this);

  no_matches_label_->setText(tr("No matches found.  Clear the search box to show the whole playlist again."));
  no_matches_label_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  no_matches_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  no_matches_label_->setWordWrap(true);
  no_matches_label_->raise();
  no_matches_label_->hide();

  // Set the colour of the no matches label to the disabled text colour
  QPalette no_matches_palette = no_matches_label_->palette();
  no_matches_palette.setColor(
      QPalette::Normal, QPalette::WindowText,
      no_matches_palette.color(QPalette::Disabled, QPalette::Text));
  no_matches_label_->setPalette(no_matches_palette);

  // Make it bold
  QFont no_matches_font = no_matches_label_->font();
  no_matches_font.setBold(true);
  no_matches_label_->setFont(no_matches_font);

  settings_.beginGroup(kSettingsGroup);

  // Icons
  ui_->clear->setIcon(IconLoader::Load("edit-clear-locationbar-ltr"));

  // Tab bar
  ui_->tab_bar->setExpanding(false);
  ui_->tab_bar->setMovable(true);

  connect(tab_bar_animation_, SIGNAL(frameChanged(int)), SLOT(SetTabBarHeight(int)));
  ui_->tab_bar->setMaximumHeight(0);

  // Connections
  connect(ui_->clear, SIGNAL(clicked()), SLOT(ClearFilter()));
  connect(ui_->tab_bar, SIGNAL(currentChanged(int)), SLOT(Save()));
  connect(ui_->tab_bar, SIGNAL(Save(int)), SLOT(SavePlaylist(int)));

  // Replace playlist search filter with native search box.
#ifdef Q_OS_DARWIN
  delete ui_->filter;
  MacLineEdit* filter = new MacLineEdit(ui_->toolbar);
  filter->setObjectName("filter");
  ui_->horizontalLayout->addWidget(filter);
  connect(filter, SIGNAL(textChanged(QString)), SLOT(UpdateFilter()));
  filter->SetHint(tr("Playlist search"));
  filter_ = filter;

  ui_->clear->hide();
#else
  filter_ = ui_->filter;
  connect(ui_->filter, SIGNAL(textChanged(QString)), SLOT(UpdateFilter()));
#endif
}

PlaylistContainer::~PlaylistContainer() {
  delete ui_;
}

PlaylistView* PlaylistContainer::view() const {
  return ui_->playlist;
}

void PlaylistContainer::SetActions(
    QAction* new_playlist, QAction* save_playlist, QAction* load_playlist) {
  ui_->create_new->setDefaultAction(new_playlist);
  ui_->save->setDefaultAction(save_playlist);
  ui_->load->setDefaultAction(load_playlist);

  ui_->tab_bar->SetActions(new_playlist, load_playlist);

  connect(new_playlist, SIGNAL(triggered()), SLOT(NewPlaylist()));
  connect(save_playlist, SIGNAL(triggered()), SLOT(SavePlaylist()));
  connect(load_playlist, SIGNAL(triggered()), SLOT(LoadPlaylist()));
}

void PlaylistContainer::ClearFilter() {
  filter_->clear();
  filter_->setFocus();
}

void PlaylistContainer::SetManager(PlaylistManager *manager) {
  manager_ = manager;
  ui_->tab_bar->SetManager(manager);

  connect(ui_->tab_bar, SIGNAL(CurrentIdChanged(int)),
          manager, SLOT(SetCurrentPlaylist(int)));
  connect(ui_->tab_bar, SIGNAL(Rename(int,QString)),
          manager, SLOT(Rename(int,QString)));
  connect(ui_->tab_bar, SIGNAL(Remove(int)),
          manager, SLOT(Remove(int)));
  connect(ui_->tab_bar, SIGNAL(PlaylistOrderChanged(QList<int>)),
          manager, SLOT(ChangePlaylistOrder(QList<int>)));

  connect(manager, SIGNAL(CurrentChanged(Playlist*)),
          SLOT(SetViewModel(Playlist*)));
  connect(manager, SIGNAL(PlaylistAdded(int,QString)),
          SLOT(PlaylistAdded(int,QString)));
  connect(manager, SIGNAL(PlaylistRemoved(int)),
          SLOT(PlaylistRemoved(int)));
  connect(manager, SIGNAL(PlaylistRenamed(int,QString)),
          SLOT(PlaylistRenamed(int,QString)));
}

void PlaylistContainer::SetViewModel(Playlist* playlist) {
  if (view()->selectionModel()) {
    disconnect(view()->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
               this, SLOT(SelectionChanged()));
  }

  // Set the view
  playlist->IgnoreSorting(true);
  view()->setModel(playlist->proxy());
  view()->SetItemDelegates(manager_->library_backend());
  view()->SetPlaylist(playlist);
  view()->selectionModel()->select(manager_->current_selection(), QItemSelectionModel::ClearAndSelect);
  playlist->IgnoreSorting(false);

  connect(view()->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
          this, SLOT(SelectionChanged()));

  // Update filter
  filter_->setText(playlist->proxy()->filterRegExp().pattern());

  // Ensure that tab is current
  if (ui_->tab_bar->current_id() != manager_->current_id())
    ui_->tab_bar->set_current_id(manager_->current_id());

  // Sort out the undo/redo actions
  delete undo_;
  delete redo_;
  undo_ = playlist->undo_stack()->createUndoAction(this);
  redo_ = playlist->undo_stack()->createRedoAction(this);
  undo_->setIcon(IconLoader::Load("edit-undo"));
  undo_->setShortcut(QKeySequence::Undo);
  redo_->setIcon(IconLoader::Load("edit-redo"));
  redo_->setShortcut(QKeySequence::Redo);

  ui_->undo->setDefaultAction(undo_);
  ui_->redo->setDefaultAction(redo_);

  emit UndoRedoActionsChanged(undo_, redo_);
}

void PlaylistContainer::ActivePlaying() {
  UpdateActiveIcon(QIcon(":tiny-start.png"));
}

void PlaylistContainer::ActivePaused() {
  UpdateActiveIcon(QIcon(":tiny-pause.png"));
}

void PlaylistContainer::ActiveStopped() {
  UpdateActiveIcon(QIcon());
}

void PlaylistContainer::UpdateActiveIcon(const QIcon& icon) {
  // Unset all existing icons
  for (int i=0 ; i<ui_->tab_bar->count() ; ++i) {
    ui_->tab_bar->setTabIcon(i, QIcon());
  }

  // Set our icon
  if (!icon.isNull())
    ui_->tab_bar->set_icon_by_id(manager_->active_id(), icon);
}

void PlaylistContainer::PlaylistAdded(int id, const QString &name) {
  int index = ui_->tab_bar->count();
  ui_->tab_bar->InsertTab(id, index, name);

  // Are we startup up, should we select this tab?
  if (starting_up_ && settings_.value("current_playlist", 1).toInt() == id) {
    starting_up_ = false;
    ui_->tab_bar->set_current_id(id);
  }

  if (ui_->tab_bar->count() > 1) {
    // Have to do this here because sizeHint() is only valid when there's a
    // tab in the bar.
    tab_bar_animation_->setFrameRange(0, ui_->tab_bar->sizeHint().height());

    if (!isVisible()) {
      // Skip the animation since the window is hidden (eg. if we're still
      // loading the UI).
      tab_bar_visible_ = true;
      ui_->tab_bar->setMaximumHeight(tab_bar_animation_->endFrame());
    } else {
      SetTabBarVisible(true);
    }
  }
}

void PlaylistContainer::PlaylistRemoved(int id) {
  ui_->tab_bar->RemoveTab(id);

  if (ui_->tab_bar->count() <= 1)
    SetTabBarVisible(false);
}

void PlaylistContainer::PlaylistRenamed(int id, const QString &new_name) {
  ui_->tab_bar->set_text_by_id(id, new_name);
}

void PlaylistContainer::NewPlaylist() {
  QString name = QInputDialog::getText(this, tr("New playlist"),
                                       tr("Enter a name for the new playlist"),
                                       QLineEdit::Normal, tr("Playlist"));
  if (name.isNull())
    return;

  manager_->New(name);
}

void PlaylistContainer::LoadPlaylist() {
  QString filename = settings_.value("last_load_playlist").toString();
  filename = QFileDialog::getOpenFileName(
      this, tr("Load playlist"), filename,
      manager_->parser()->filters());

  if (filename.isNull())
    return;

  settings_.setValue("last_load_playlist", filename);

  manager_->Load(filename);
}

void PlaylistContainer::SavePlaylist(int id = -1) {
  QString filename = settings_.value("last_save_playlist").toString();

  // We want to use the playlist tab name as a default filename, but in the
  // same directory as the last saved file.

  // Strip off filename components until we find something that's a folder
  forever {
    QFileInfo fileinfo(filename);
    if (filename.isEmpty() || fileinfo.isDir())
      break;

    filename = filename.section('/', 0, -2);
  }

  // Use the home directory as a fallback in case the path is empty.
  if (filename.isEmpty())
    filename = QDir::homePath();

  // Add the suggested filename based on the tab name
  filename += "/" + ui_->tab_bar->tabText(ui_->tab_bar->currentIndex()) +
              "." + manager_->parser()->default_extension();

  QString default_filter = manager_->parser()->default_filter();

  filename = QFileDialog::getSaveFileName(
      this, tr("Save playlist"), filename,
      manager_->parser()->filters(), &default_filter);

  if (filename.isNull())
    return;

  settings_.setValue("last_save_playlist", filename);

  manager_->Save(id == -1 ? manager_->current_id() : id, filename);
}

void PlaylistContainer::Save() {
  if (starting_up_)
    return;

  settings_.setValue("current_playlist", ui_->tab_bar->current_id());
}

void PlaylistContainer::SetTabBarVisible(bool visible) {
  if (tab_bar_visible_ == visible)
    return;
  tab_bar_visible_ = visible;

  tab_bar_animation_->setDirection(visible ? QTimeLine::Forward : QTimeLine::Backward);
  tab_bar_animation_->start();
}

void PlaylistContainer::SetTabBarHeight(int height) {
  ui_->tab_bar->setMaximumHeight(height);
}

void PlaylistContainer::UpdateFilter() {
  manager_->current()->proxy()->setFilterFixedString(filter_->text());
  ui_->playlist->JumpToCurrentlyPlayingTrack();

  bool no_matches = manager_->current()->proxy()->rowCount() == 0 &&
                    manager_->current()->rowCount() > 0;

  if (no_matches)
    RepositionNoMatchesLabel(true);
  no_matches_label_->setVisible(no_matches);
}

void PlaylistContainer::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  RepositionNoMatchesLabel();
}

void PlaylistContainer::RepositionNoMatchesLabel(bool force) {
  if (!force && !no_matches_label_->isVisible())
    return;

  const int kBorder = 10;

  QPoint pos = ui_->playlist->viewport()->mapTo(this, QPoint(kBorder, kBorder));
  QSize size = ui_->playlist->viewport()->size();
  size.setWidth(size.width() - kBorder * 2);
  size.setHeight(size.height() - kBorder * 2);

  no_matches_label_->move(pos);
  no_matches_label_->resize(size);
}

void PlaylistContainer::SelectionChanged() {
  manager_->SelectionChanged(view()->selectionModel()->selection());
}
