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

#include "echonestimages.h"

#include <echonest/Artist.h>

#include <boost/scoped_ptr.hpp>

struct EchoNestImages::Request {
  Request(int id) : id_(id), artist_(new Echonest::Artist) {}

  int id_;
  boost::scoped_ptr<Echonest::Artist> artist_;
};

void EchoNestImages::FetchInfo(int id, const Song& metadata) {
  boost::shared_ptr<Request> request(new Request(id));
  request->artist_->setName(metadata.artist());

  QNetworkReply* reply = request->artist_->fetchImages();
  connect(reply, SIGNAL(finished()), SLOT(RequestFinished()));
  requests_[reply] = request;
}

void EchoNestImages::RequestFinished() {
  QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
  if (!reply || !requests_.contains(reply))
    return;
  reply->deleteLater();

  RequestPtr request = requests_.take(reply);

  try {
    request->artist_->parseProfile(reply);
  } catch (Echonest::ParseError e) {
    qWarning() << "Error parsing echonest reply:" << e.errorType() << e.what();
  }

  foreach (const Echonest::ArtistImage& image, request->artist_->images()) {
    emit ImageReady(request->id_, image.url());
  }

  emit Finished(request->id_);
}
