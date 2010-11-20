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

#ifndef LIBRARYQUERY_H
#define LIBRARYQUERY_H

#include <QString>
#include <QVariant>
#include <QSqlQuery>
#include <QStringList>
#include <QVariantList>

class Song;
class LibraryBackend;

struct QueryOptions {
  QueryOptions();

  bool Matches(const Song& song) const;

  QString filter;
  int max_age;
};

class LibraryQuery {
 public:
  LibraryQuery(const QueryOptions& options = QueryOptions());

  void SetColumnSpec(const QString& spec) { column_spec_ = spec; }
  void SetOrderBy(const QString& order_by) { order_by_ = order_by; }
  void AddWhere(const QString& column, const QVariant& value, const QString& op = "=");
  void AddCompilationRequirement(bool compilation);
  void SetLimit(int limit) { limit_ = limit; }

  QSqlError Exec(QSqlDatabase db, const QString& songs_table, const QString& fts_table);
  bool Next();
  QVariant Value(int column) const;

  operator const QSqlQuery& () const { return query_; }

 private:
  bool join_with_fts_;
  QString column_spec_;
  QString order_by_;
  QStringList where_clauses_;
  QVariantList bound_values_;
  int limit_;

  QSqlQuery query_;
};

#endif // LIBRARYQUERY_H
