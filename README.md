# os161 installation

1) Download https://drive.google.com/file/d/1gwEmXNjjaP3Jf46ZyEe3nt5lIMSeXeX6/view?usp=sharing
2) Unzip the file and follow instructions in README.txt (no need to use the command tar -zxvf os161-kit.tgz mentioned in the readme as you already unzipped the archive, just put it where it says). Be sure to use a non-root user during the procedure.
3) If bmake does not work, install it with apt
4) In order to use the git repository:
   1) cp $HOME/os161 $HOME/os161backUp (make a backUp copy)
   2) cd $HOME/os161
   3) rm -rf os161-base-2.0.3
   4) git clone "linkToThisRepo"
   5) mv os161-project os161-base-2.0.3
   Now all the files inside os161-base-2.0.3 are from the repo. Just cd os161-base-2.0.3 and use git commands and usual.
   
