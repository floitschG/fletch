// Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

import 'package:immi/dart/immi_service_impl.dart';

import 'package:github_sample/src/commit_list_presenter.dart';
import 'package:github_sample/src/commit_presenter.dart';
import 'package:github_sample/src/github_services.dart';

import 'package:immi_samples/drawer.dart';
import 'package:immi_samples/menu.dart';
import 'package:immi_samples/login_presenter.dart';

main() {
  var server = new Server.invertedForTesting(8321);
  var user = server.getUser('dart-lang');
  var repo = user.getRepository('fletch');

  var menu = new Menu('Menu');
  var commits = new CommitListPresenter(repo);
  var login = new LoginPresenter('https://api.github.com/user/repos');
  var drawer = new Drawer(commits, left: menu, right: login);

  menu.add(new MenuItem('Commits @ Fletch', () { drawer.center = commits; }));

  var impl = new ImmiServiceImpl();
  impl.add('DrawerPresenter', drawer);

  impl.run();
  server.close();
}
