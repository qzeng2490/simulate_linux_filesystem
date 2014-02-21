#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#define BLOCKSIZ    512		//磁盘块的大小
#define DATABLKNUM  512		//数据块的数目
#define BLKGRUPNUM	50		//数据块组包含多少数据块
#define P_N_BLOCKS	15		//inode节点中 指向数据块的指针个数
#define	GROUPNUM	DATABLKNUM/BLKGRUPNUM+1 //数据块组 组数

#define DINODESIZ   512		//磁盘i结点区的大小（空间32×512）
#define DINODENUM   32		//磁盘i结点区的块数

#define SYSOPENFILE 40
#define DIRNUM      32		//一个目录下的最多目录和文件的总和数
#define DIRSIZ      14		//文件、目录名的长度(字节)
#define UPWDSIZ     15		//秘密的长度
#define UNAMSIZ     15		//用户名的长度
#define PWDSIZ		sizeof(struct pwd) //密码结构的长度
#define PWDNUM      BLOCKSIZ/PWDSIZ		//密码数据空间的大小（pwd为单位）

#define USERNUM     10		//用户名的长度
#define DINODESTART 4*BLOCKSIZ	//i结点区的开始地址-inodes table ，1引导 2超块 3block bitmap 4inode bitmap
#define DATASTART   (4+DINODENUM)*BLOCKSIZ	//数据区的开始地址
#define	DATASTARTNO	36		//数据区开始指针

/*  di._mode  */
#define DIMODE_EMPTY	00000/*可以用的空间*/
#define DIMODE_FILE		00001
#define DIMODE_DIR      00002
#define DIMODE_PASSWD	00004
#define DIMODE_SYSTEM	00040	/*系统文件*/

/*组*/
#define GRUP_0			0	//管理员组
#define GRUP_1			1
#define GRUP_2			2
#define GRUP_4			4


/************************    文件系统 数据结构  *******************************/

/*————————————————————————
	磁盘i结点结构，

————————————————————————*/
struct inode{
	//	 char				di_name[DIRSIZ];
		 unsigned int  di_ino;	/*磁盘i节点标识*/
	     unsigned int  di_number;	/*关联文件数，当为0时表示删除文件*/
	     unsigned int  di_mode;	/*存取权限*/
	     unsigned int  di_uid;	/*磁盘i节点用户id*/
	     unsigned int  di_gid;	/*磁盘i节点权限组id*/ //1管理员组 2用户组
	     unsigned int  di_size;	/*文件大小*/
		 unsigned int  di_ctime;   /* Creation time */
		 unsigned int  di_mtime;   /* Modification time */
		 unsigned int  di_block[P_N_BLOCKS]; /* 一组 block 指针 */
	     };

/*————————————————————————
	目录项结构
————————————————————————*/
struct direct{
		char	d_name[DIRSIZ];	/*目录名（14字节）*/
		int d_ino;	/*目录号*/
		};

/*————————————————————————
	超级快结构

————————————————————————*/
struct super_block{
		unsigned int s_inodes_count;      /* inodes 计数 */
		unsigned int s_blocks_count;      /* blocks 计数 */
		unsigned int s_r_blocks_count;    /* 保留的 blocks 计数 */
		unsigned int s_free_blocks_count; // 空闲的 blocks 计数
		unsigned int s_free_inodes_count; /* 空闲的 inodes 计数 */
		unsigned int s_free_blocks_group[GROUPNUM];//新增 一个数组来记录每个数据块组中的空闲数据块计数
		unsigned int s_first_data_block;  /* 第一个数据 block */
		unsigned int s_log_block_size;    /* block 的大小 */
		unsigned int s_blocks_per_group;  /* 每 block group 的 block 数量 */
		unsigned int s_inodes_per_group;  /* 每 block group 的 inode 数量 */

		};

/*————————————————————————
	用户密码
————————————————————————*/
struct pwd{
		unsigned int p_uid;
		unsigned int p_gid;
		char username[UNAMSIZ];/*用户名  新加的*/
		char password[UPWDSIZ];
		};

/*————————————————————————
	目录结构
————————————————————————*/
struct dir{
		struct direct direct[DIRNUM];
		int size;
		};
//		全局变量

unsigned int di_bitmap[DINODENUM];	// 硬盘inode节点位图  1表示已使用 0表示未使用
unsigned int	bk_bitmap[DATABLKNUM];	// 数据块block位图
struct super_block filsys;				//超级块
struct pwd pwd[PWDNUM];
int usernum;
struct pwd passwd [BLOCKSIZ/PWDSIZ];
FILE   *fd;								//文件指针
struct inode *cur_inode;				//i节点当前目录指针
struct inode *inodetemp;				//i节点指针
const char   fsystemname[20]="Linux.EXT2";	//模拟硬盘的文件名
struct direct dir_buf[BLOCKSIZ / sizeof(struct direct)];	//目录数组
char cmdhead[20];//cmd 的头 表示所在哪个文件夹	、
int i_lock=0;//inode位图锁 可能会多线程
int b_lock=0;//block位图锁
struct pwd *cur_user;

/*		全局函数		*/
extern int	Format();//格式化磁盘
extern int	Install();//启动，安装文件系统


struct inode * read_inode(int);//install里面读取文件dinode
struct direct * read_dir_data(int);//读取存储文件夹的物理块
extern  void showdir();//命令 dir
int Enterdir(char[]);//进入某个文件夹 命令-- cd 文件名
int Fd_dirfile(char[]);//查找当前目录里的文件 没找到返回-1 找到返回inode号
int Iscmd(char[]);//判断是否两个字符串的命令
void cmd_Up(char[],char[]);//两个字符串的命令
int create(char[]);//创建文件
void changeinode();//交换指针
char * ReadFile(char[]);//读取文件
int cdir(char[]);//创建文件夹
void showbitmap();//显示位图
int deletefd(char[]);//删除文件
int mywrite(char[]);//编辑文件
int adduser(char strname[]);
void showhelp();//命令帮助
void login();
int myaccess();//权限判断
/*磁盘i节点的分配与释放（当一个新文件被建立的时候，在给该文件分配磁盘存储区之前，
应为该文件分配存放该文件说明信息的磁盘i节点，当从文件系统中删除某个文件时，
应首先删除它的磁盘i节点项。）*/
int		ialloc();/*开辟一个空闲的i节点，返回i节点*/

//磁盘块分配与释放函数
int		balloc(int);//申请硬盘空间
void showaccess(char strname[20]);

void main()
{
		char str[10];
		char strname[10];
		char c;

		printf("是否格式化?<y/n>");
		scanf("%c",&c);
		fgetc(stdin);
		if(c=='y')
		{
			if(!Format())
			{
				return;
			}
			printf("格式化完毕!\n");
		}

		if(!Install())
		{
			return;
		}
		printf("login.................\n");
		login();
		showhelp();
		printf("%s>",cmdhead);
		while(1)
		{
			scanf("%s",&str);
			if(strcmp(str,"exit")==0)
			{
				fclose(fd);
				return;
			}
			else	if(strcmp(str,"dir")==0)
					{
						showdir();
					}
			else if(strcmp(str,"bit")==0)
			{
					showbitmap();
			}
			else if(strcmp(str,"help")==0)
			{
					showhelp();
			}
			
			else	if(Iscmd(str))
			{
				scanf("%s",&strname);
				cmd_Up(str,strname);
			}

			else
			{
				printf("错误命令!\n");
			}
			printf("%s>",cmdhead);
		}

}

//---------------------------格式化------------
int Format()
{


	int i;

	/*	creat the file system file */
	fd = fopen (fsystemname, "wb");/*读写创建一个二进制文件*/

	if(fd==NULL)
	{
		printf("硬盘模拟文件创建失败!\n");
		return 0;
	}
	//超级块
	filsys.s_inodes_count=DINODENUM ;      /* inodes 计数 */
	filsys.s_blocks_count=DATABLKNUM;      /* blocks 计数 */
	filsys. s_r_blocks_count=0;    /* 保留的 blocks 计数 */
	filsys. s_free_blocks_count=DATABLKNUM-5; /* 空闲的 blocks 计数 */
	filsys.s_free_blocks_group[0]=50-5;//第一个block group 已经被用了5个
	for(i=1;i<GROUPNUM-1;i++)
	{
		filsys.s_free_blocks_group[i]=50; //后面的group 全部空闲
	}
	filsys.s_free_blocks_group[GROUPNUM-1]=12;//最后一个block组 只有12个block
	filsys.s_free_inodes_count=DINODENUM-5; /* 空闲的 inodes 计数 */
	filsys.s_first_data_block=DATASTARTNO;  /* 第一个数据 block 也就是*/
	filsys.s_log_block_size=BLOCKSIZ;    /* block 的大小 */
	filsys.s_blocks_per_group=BLKGRUPNUM;  /* 每 block group 的 block 数量 */
	filsys.s_inodes_per_group=0;  //每 block group 的 inode 数量  暂未使用

	fseek(fd, BLOCKSIZ, SEEK_SET);
	fwrite (&filsys,BLOCKSIZ, 1,fd);

	//初始化dinode位图 block位图
	di_bitmap[0]=1;
	di_bitmap[1]=1;
	di_bitmap[2]=1;//前三个inode 分别被 root etc 用户passwd文件占用
	di_bitmap[3]=1;
	di_bitmap[4]=1;

	bk_bitmap[0]=1;
	bk_bitmap[1]=1;
	bk_bitmap[2]=1;//前三个inode 分别被 root etc 用户passwd文件占用
	bk_bitmap[3]=1;
	bk_bitmap[4]=1;

	for(i=5;i<DINODENUM;i++)
	{
		di_bitmap[i]=0;
		bk_bitmap[i]=0;
	}
	for(;i<DATABLKNUM;i++)
	{
		bk_bitmap[i]=0;
	}
	fseek(fd, BLOCKSIZ*2, SEEK_SET);
	fwrite (di_bitmap,BLOCKSIZ, 1,fd);
	fseek(fd, BLOCKSIZ*3, SEEK_SET);
	fwrite (bk_bitmap,BLOCKSIZ, 1,fd);


	//初始化主目录
	struct 	inode *ininode;
	ininode=(struct inode *)malloc(sizeof (struct inode));
	if(!ininode)
	{
		printf("ininode 内存分配失败!");
		return 0;
	}
	ininode->di_ino=0;//i节点标志
	ininode->di_number=3;//关联3个文件夹
	ininode->di_mode=DIMODE_DIR|DIMODE_SYSTEM;//0为目录
	ininode->di_uid=1;//用户id 第一个用户
	ininode->di_gid=1;//组id 管理员组
	ininode->di_size=0;//为目录
	ininode->di_ctime=0;   /* Creation time */
	ininode->di_mtime=0;   /* Modification time */
	ininode->di_block[0]=0;//所占物理块号 后3块分别是 一级指针，二级指针，3级指针

	fseek(fd,DINODESTART, SEEK_SET);
	fwrite(ininode,sizeof(struct inode), 1, fd);


	strcpy(dir_buf[0].d_name, ".");
	dir_buf[0].d_ino= 0;//当前目录的dinode号
	strcpy(dir_buf[1].d_name,"..");
	dir_buf[1].d_ino= 0;//主目录的上级目录还是自己
	strcpy(dir_buf[2].d_name, "etc");
	dir_buf[2].d_ino = 1;//etc目录

	fseek(fd, DATASTART, SEEK_SET);
	fwrite(dir_buf, BLOCKSIZ, 1, fd);

	//etc目录
	ininode->di_ino=1;//i节点标志
	ininode->di_number=5;//
	ininode->di_gid=1;//组id
	ininode->di_block[0]=1;//所占物理块号 后3块分别是 一级指针，二级指针，3级指针
	fseek(fd, DINODESTART+BLOCKSIZ, SEEK_SET);
	fwrite(ininode,sizeof(struct inode), 1, fd);

	strcpy (dir_buf[0].d_name, ".");
	dir_buf[0].d_ino = 1;
	strcpy(dir_buf[1].d_name, "..");
	dir_buf[1].d_ino = 0;
 	strcpy(dir_buf[2].d_name, "passwd");
 	dir_buf[2].d_ino = 2;
	strcpy(dir_buf[3].d_name, "admin");
 	dir_buf[3].d_ino = 3;
	strcpy(dir_buf[4].d_name, "zq");
 	dir_buf[4].d_ino = 4;

	fseek(fd, DATASTART+BLOCKSIZ, SEEK_SET);
	fwrite (dir_buf, BLOCKSIZ,1,fd);

	// admin 目录
	ininode->di_ino=3;//i节点标志
	ininode->di_number=2;//
    ininode->di_gid=0;//组id
	ininode->di_block[0]=3;//所占物理块号 后3块分别是 一级指针，二级指针，3级指针
	fseek(fd, DINODESTART+BLOCKSIZ*3, SEEK_SET);
	fwrite(ininode,sizeof(struct inode), 1, fd);

	strcpy (dir_buf[0].d_name, ".");
	dir_buf[0].d_ino = 3;
	strcpy(dir_buf[1].d_name, "..");
	dir_buf[1].d_ino = 1;


	fseek(fd, DATASTART+BLOCKSIZ*3, SEEK_SET);
	fwrite (dir_buf, BLOCKSIZ,1,fd);

	// zq 目录
	ininode->di_ino=4;//i节点标志
	ininode->di_number=2;//
	ininode->di_uid=2;//用户id
	ininode->di_gid=1;//组id
	ininode->di_block[0]=4;//所占物理块号 后3块分别是 一级指针，二级指针，3级指针
	fseek(fd, DINODESTART+BLOCKSIZ*4, SEEK_SET);
	fwrite(ininode,sizeof(struct inode), 1, fd);

	strcpy (dir_buf[0].d_name, ".");
	dir_buf[0].d_ino =4;
	strcpy(dir_buf[1].d_name, "..");
	dir_buf[1].d_ino = 1;

	fseek(fd, DATASTART+BLOCKSIZ*4, SEEK_SET);
	fwrite (dir_buf, BLOCKSIZ,1,fd);


	//用户passwd文件
	passwd[0].p_uid= 1;
	passwd[0].p_gid = GRUP_0; //管理员
	strcpy(passwd[0].username, "admin");
	strcpy(passwd[0].password, "admin");

	passwd[1].p_uid= 2;
	passwd[1].p_gid = GRUP_1;
	strcpy(passwd[1].username, "zq");
	strcpy(passwd[1].password, "zq");
   // usernum=2;

	for (i=2; i<PWDNUM; i++)
	{
		passwd[i].p_uid = 0;
		passwd[i].p_gid = GRUP_4;
		strcpy(passwd[i].username, "");
		strcpy(passwd[i].password,"");
	}
	fseek(fd,DATASTART+BLOCKSIZ*2, SEEK_SET);
	fwrite(passwd,BLOCKSIZ,1,fd);

	ininode->di_ino=2;//i节点标志
	ininode->di_number=2;//
	ininode->di_mode=DIMODE_PASSWD|DIMODE_SYSTEM;//
	ininode->di_uid=1;//用户id 第一个用户
	ininode->di_gid=1;//组id 管理员组
	ininode->di_size=BLOCKSIZ;//大小
	ininode->di_ctime=0;   /* Creation time */
	ininode->di_mtime=0;   /* Modification time */
	ininode->di_block[0]=2;//所占物理块号 后3块分别是 一级指针，二级指针，3级指针
	fseek(fd, DINODESTART+BLOCKSIZ*2, SEEK_SET);
	fwrite(ininode,sizeof(struct inode), 1, fd);

	fclose(fd);
	free(ininode);
	return 1;
}

int	Install()
{
	int i;
	printf("install...\n");
	fd = fopen (fsystemname, "rb+");// 只读方式打开硬盘模拟文件
	if(fd==NULL)
	{
		printf("文件打开失败\n");
		return 0;
	}


	fseek(fd,BLOCKSIZ,SEEK_SET);
	fread(&filsys,sizeof(struct super_block),1,fd);


	inodetemp=(struct inode *)malloc(sizeof (struct inode));
	if(!inodetemp)
	{
		printf("inodetemp 内存分配失败!\n");
		return 0;
	}
	cur_inode=(struct inode *)malloc(sizeof (struct inode));
	if(!cur_inode)
	{
		printf("cur_inode 内存分配失败!\n");
		return 0;
	}

	//读取inode bitmap
	fseek(fd,BLOCKSIZ*2,SEEK_SET);
	fread(di_bitmap,BLOCKSIZ,1,fd);
	//读取block bitmap
	fseek(fd,BLOCKSIZ*3,SEEK_SET);
	fread(bk_bitmap,BLOCKSIZ,1,fd);

	//读取用户passwd文件的inode
	inodetemp=read_inode(2);
	if(inodetemp==NULL)
	{
		return 0;
	}
	changeinode();//交换指针后 cur_inode 指向当前目录的 inode
	//读取用户passwd文件的数据区
	fseek(fd, DATASTART+BLOCKSIZ*2, SEEK_SET);
	fread(pwd,BLOCKSIZ, 1, fd);

	for(i=0;i<PWDNUM;i++)
	{
		if(pwd[i].p_uid!=0)
		{
			printf("%s,%s\n",&pwd[i].username,&pwd[i].password);
		}

	}


	inodetemp=read_inode(0);//读取主目录的inode;
	if((inodetemp->di_mode&DIMODE_DIR)!=DIMODE_DIR)
	{
		printf("读取主目录失败,请重新格式化!\n");
	}
	else
	{
		changeinode();//交换指针    cur_inode 指向当前目录的 inode
		read_dir_data(cur_inode->di_block[0]);

	}

//	fclose(fd);
	return 1;
}

/*----------------------------------
read_dir_data(int)
读取存储文件夹的物理块
----------------------------------*/
struct direct * read_dir_data(int n)
{

	fseek(fd, DATASTART+BLOCKSIZ*n, SEEK_SET);
	fread (dir_buf, cur_inode->di_number*(sizeof(struct direct)),1,fd);

	return dir_buf;
}

struct inode * read_inode(int n)//读取文件的dinode
{
	int i;

	fseek(fd,DINODESTART+BLOCKSIZ*n,SEEK_SET);
	fread(inodetemp,sizeof(struct inode),1,fd);


	if(inodetemp->di_ino!=n)
	{
		printf("size=%d,number=%d,block=%d\n",inodetemp->di_size,inodetemp->di_number,inodetemp->di_block[0]);
		printf("inode读取错误!不是%d,而是%d\n",inodetemp->di_ino,n);
		return NULL;
	}

	i=0;
	do{
		inodetemp->di_block[i]=inodetemp->di_block[i];//物理块号 后3块分别是 一级指针，二级指针，3级指针
		if(i>=P_N_BLOCKS-3)//该文件太大
		{
			printf("该文件太大，2级指针还没实现!\n");
			break;
		}
		i++;
	}while(i<(int)inodetemp->di_size/BLOCKSIZ);//用do while是因为按照size来判断占用多少物理块号
											//目录时是0 文件有时候是恰好512 他们除以512 是0或1

	return inodetemp;

}

void showdir()
{
	int i;

	for(i=0;i<cur_inode->di_number;i++)
	{

		if(i==0)
		{
			printf("\t.\t\t\t<dir>\tinode %d\n",dir_buf[i].d_ino);

		}
		else	if(i==1)
		{
			printf("\t..\t\t\t<dir>\tinode %d\n",dir_buf[i].d_ino);
		}
		else
		{

			inodetemp=read_inode(dir_buf[i].d_ino);
			if((inodetemp->di_mode&DIMODE_DIR)==DIMODE_DIR)
			{
				printf("\t%s\t\t\t<dir>\tinode %d\n",dir_buf[i].d_name,dir_buf[i].d_ino);
			}
			else if((inodetemp->di_mode&DIMODE_FILE)==DIMODE_FILE)
			{
				printf("\t%s\t\t\t<file>\tsize %d block %d\n",dir_buf[i].d_name,inodetemp->di_size,inodetemp->di_block[0]);
			}
			else
			{
				printf("\t%s\t\t\t<passwd>inode %d block %d\n",dir_buf[i].d_name,dir_buf[i].d_ino,inodetemp->di_block[0]);
			}

		}
	}
}

int Enterdir(char* namestr)//进入目录
{
	int i;
	i=Fd_dirfile(namestr);
	if(i!=-1)
	{

		read_inode(i);
		if((inodetemp->di_mode&DIMODE_DIR)!=DIMODE_DIR)
		{
			printf("%s 不是目录!\n",namestr);
			return -1;
		}

		if(!myaccess())
		{
			printf("您没有权限!\n");
			return -1;
		}

		changeinode();
	}
	else
	{
		printf("未找到该文件!请输入正确的文件或目录名\n");
		return -1;
	}
	read_dir_data(cur_inode->di_block[0]);

	return 1;
}

int Fd_dirfile(char namestr[20])//在当前目录下查找目录和文件
{

	int i=0;
	do{
		if(strcmp(".",namestr)==0)
		{
			i=0;
			break;
		}
		if(strcmp("..",namestr)==0)
		{
					i=1;
					break;
		}
		if(strcmp(dir_buf[i].d_name,namestr)==0)
		{
			if(dir_buf[i].d_ino!=-1)
			{
				break;
			}

		}
		i++;
	}while(i<cur_inode->di_number);
	if(i==cur_inode->di_number)
	{
		return(-1);
	}
 return (dir_buf[i].d_ino);
}

int Iscmd(char cmd[10])
{
	if(!strcmp(cmd,"cd") ||
		!strcmp(cmd,"cdir") ||
		!strcmp(cmd,"create") ||
		!strcmp(cmd,"read") ||
		!strcmp(cmd,"write") ||
		!strcmp(cmd,"del")||
		!strcmp(cmd,"adduser"))

		return 1;
	else
		return 0;
}

void cmd_Up(char str[10],char strname[14])
{
	int l,i,itemp;
	char *a;
	l=strlen(strname);
	if(l>=14)
	{
		printf("文件名过长!\n");
		return;
	}
	if(strcmp(str,"cd")==0)
	{
		if(Enterdir(strname)>=0)
		{

			l=strlen(cmdhead);

		//	printf("进入目录成功!\n");
			if(strcmp(strname,"..")==0)
			{
					if(strcmp(cmdhead,"root")!=0)
					{
						i=0;
						while(cmdhead[i]!='\0')
						{

							if(cmdhead[i]=='\\')
								{
									itemp=i;

								}
							i++;
						}
						cmdhead[itemp]='\0';

					}

			}
			else if(strcmp(strname,".")!=0)
			{

				strcat(cmdhead,"\\");
				strcat(cmdhead,strname);
			}
		}
		else
		{
			printf("进入目录失败!\n");
		}

	}
	else if(strcmp(str,"create")==0)
	{
		if(create(strname))
			{
			//	printf("文件创建成功!\n");
			}
			else
			{
				printf("文件创建失败!\n");
			}

	}
	else if(strcmp(str,"read")==0)
	{
		char * buf;
		buf=ReadFile(strname);
		if(buf==NULL)
		{
			printf("读取失败!\n");
		}
		else
		{
			printf("所读文件内容：\n%s\n",buf);
		}

		free(buf);
	}
	else if(strcmp(str,"cdir")==0)
	{
		if(cdir(strname))
		{
		//	printf("文件夹创建成功!\n");
		}
		else
		{
			printf("文件夹创建失败!\n");
		}
	}
	else if(strcmp(str,"adduser")==0)
	{
		if(adduser(strname))
		{
		//	printf("文件夹创建成功!\n");
		}
		else
		{
			printf("用户创建失败!\n");
		}
	}
	else if(strcmp(str,"del")==0)
	{
		if(deletefd(strname))
		{
		//	printf("%s成功删除!\n",strname);
		}
		else
		{
			printf("%s删除失败!\n",strname);
		}


	}
	else if(strcmp(str,"write")==0)
	{
		if(mywrite(strname))
		{
				printf("%s保存成功!\n",strname);
		}
		else
		{
				printf("%s保存失败!\n",strname);
		}

	}
	
	else
	{
		printf("错误命令!\n");
	}
}

/*-----------------
创建文件
-----------------*/
int create(char strname[14])
{
	int fi,inum,bnum;
	fi=Fd_dirfile(strname);//查找该文件夹下是否有文件
	if(fi!=-1)
	{
		printf("该文件夹下已有 %s 文件或文件夹\n",strname);
		return 0;
	}

	fgetc(stdin);//开始输入文件内容
	char *buf;
	int i,k;
	i = 0;k=0;

	buf = (char *)malloc(BLOCKSIZ*sizeof(char));

	printf("输入文件内容，以\"###\"结束:\n");
	while(k!=3)
	{
		buf[i] = getchar();
		if(buf[i] == '#')
		{
			k++;
			if(k == 3)
				break;
		}
		else
			k=0;
		i++;
		if(i/BLOCKSIZ > 0)
		{
			buf = (char *)realloc(buf,BLOCKSIZ*(i/BLOCKSIZ+1));
		}
	}
	buf[i-2]='\0';

	printf("buf length %d\n",strlen(buf));

	inum=ialloc();//申请inode空间
	if(inum<=-1)
	{
		printf("inode 申请失败!\n");
		return 0;
	}

	bnum=balloc(strlen(buf)/BLOCKSIZ+1);//申请block
	if(bnum<=-1)
	{
		printf("block 申请失败!\n");
		//释放申请成功的inode
		di_bitmap[inum]=0;//inode位图置0 表示空闲
		filsys.s_free_inodes_count++;
		return 0;
		}

	inodetemp->di_ino=inum;//i节点标志
	inodetemp->di_number=1;//关联一个文件
	inodetemp->di_mode=DIMODE_FILE;//0为目录
	inodetemp->di_uid=cur_user->p_uid;//用户id 第一个用户
	inodetemp->di_gid=cur_user->p_gid;//组id 管理员组
	inodetemp->di_size=strlen(buf);//
	inodetemp->di_ctime=0;   /* Creation time */
	inodetemp->di_mtime=0;   /* Modification time */
	for(i=0;i<(int)inodetemp->di_size/BLOCKSIZ+1;i++)
	{
		inodetemp->di_block[i]=bnum+i;
	}
	fseek(fd,DATASTART+BLOCKSIZ*bnum, SEEK_SET);//将新建的文件写入硬盘data区
	fwrite(buf,BLOCKSIZ*(inodetemp->di_size/BLOCKSIZ+1), 1, fd);

	fseek(fd,DINODESTART+BLOCKSIZ*inum, SEEK_SET);//将新建的文件信息写入硬盘inode区
	fwrite(inodetemp,BLOCKSIZ, 1, fd);

	dir_buf[cur_inode->di_number].d_ino=inum;//修改当前目录的结构
	strcpy(dir_buf[cur_inode->di_number].d_name,strname);

	fseek(fd,DATASTART+BLOCKSIZ*cur_inode->di_block[0], SEEK_SET);//将当前目录信息写入文件的block区
	fwrite(dir_buf,BLOCKSIZ, 1, fd);

	cur_inode->di_number++;//当前目录关联文件数++
	printf("当前目录文件数%d\n",cur_inode->di_number);

	fseek(fd,DINODESTART+BLOCKSIZ*cur_inode->di_ino, SEEK_SET);//将当前目录信息写入文件的inode区
	fwrite(cur_inode,sizeof(struct inode), 1, fd);

	filsys.s_free_blocks_count--;
	filsys.s_free_blocks_group[inodetemp->di_block[0]/GROUPNUM]--;
	filsys.s_free_inodes_count--;


	fseek(fd, BLOCKSIZ, SEEK_SET);//修改超级块 free block计数 free inode计数 和每组free block 计数
	fwrite (&filsys,BLOCKSIZ, 1,fd);

	fseek(fd, BLOCKSIZ*2, SEEK_SET);//在硬盘修改inode位图
	fwrite (di_bitmap,BLOCKSIZ, 1,fd);

	fseek(fd, BLOCKSIZ*3, SEEK_SET);//在硬盘修改block位图
	fwrite (bk_bitmap,BLOCKSIZ, 1,fd);
	free(buf);
	return 1;
}

void changeinode()//交换 cur_inode和inodetemp 指针
{
	struct inode *intemp;
	intemp=cur_inode;
	cur_inode=inodetemp;
	inodetemp=intemp;
}

/*-----------------
开辟一个空闲的i节点，返回i节点
--------------------*/
int ialloc()
{
	int i;
	if(i_lock==1)//检测是否加锁
	{
		printf("inode位图区已锁!稍后再试!\n");
		i_lock=0;//解锁
		return -1;
	}
	i_lock=1;//将inode位图加锁
	if(filsys.s_free_inodes_count<=0)
	{
		printf("inode已满!申请失败\n");
		i_lock=0;//inode位图解锁
		return -1;
	}


	//在inode bitmap寻找空闲的inode
	for(i=0;i<DINODENUM;i++)
	{
		if(di_bitmap[i]==0)
		{
		//	printf("已经找到空闲的inode %d\n",i);
			di_bitmap[i]=1;//inode位图置1 表示已被占用
			filsys.s_free_inodes_count--;
			i_lock=0;//inode位图解锁
			return i;

		}
	}


	i_lock=0;//inode位图解锁
	return -1;

}



/*-------------------------
balloc
/申请硬盘空间/
------------------------*/
int  balloc(int k)
{
	printf("申请连续%d块\n",k);
	int bnum,i,j,n,g;
	if(b_lock==1)
	{
		printf("block位图区已锁!稍后重试!\n");
		return -1;
	}
	b_lock=1;//加锁
	n=BLKGRUPNUM;
	if(filsys.s_free_blocks_count<=0)
	{
		printf("磁盘已满!申请失败\n");
		b_lock=0;//解锁
		return -1;
	}

	//在block bitmap寻找空闲的inode
	for(i=0;i<GROUPNUM;i++)
	{
		if(filsys.s_free_blocks_group[i]<k)//先找到有空闲的block组
		{
			continue;
		}
		if(i>=GROUPNUM-1)
		{
			n=DATABLKNUM%BLKGRUPNUM;//最后那个数据块只有12块
		}
		g=0;
		for(j=0;j<n;j++)
		{
			if(bk_bitmap[i*GROUPNUM+j]==0)
			{
				bnum=i*GROUPNUM+j;
				g++;
				filsys.s_free_blocks_group[i]--;
				if(g==k)//如果找到足够的空闲块
				{
					b_lock=0;//解锁
					printf("已经找到空闲的block %d 它在第%d组\n",bnum,i);

					for(i=0;i<k;i++)
					{
						bk_bitmap[bnum-i]=1;
					}

					return bnum-k+1;

				}

			}
			else
			{
				g=0;
			}
		}
	}

	printf("error!没有找到足够多连续空闲的block!\n");
	b_lock=0;//解锁
	return -1;

}


char* ReadFile(char strname[14])
{
	int fi;
	fi=Fd_dirfile(strname);//查找该文件夹下是否有文件
	if(fi!=-1)
	{
	//	printf("已找到 %s 文件\n",strname);
	}
	else
	{
		printf("找不到到 %s 文件\n",strname);
		return NULL;
	}
	inodetemp=read_inode(fi);
	if(!myaccess())
	{
			printf("您没有权限!\n");
			return NULL;
	}

	if(inodetemp==NULL)
	{
		return NULL;
	}
	if((inodetemp->di_mode&DIMODE_FILE)!=DIMODE_FILE)
	{
		printf("%s不是一个文件!\n",strname);
		return NULL;
	}

	char *buf;
	buf = (char *)malloc(BLOCKSIZ*(inodetemp->di_size/BLOCKSIZ+1));

	fseek(fd, DATASTART+BLOCKSIZ*inodetemp->di_block[0], SEEK_SET);
	fread (buf, BLOCKSIZ*(inodetemp->di_size/BLOCKSIZ+1),1,fd);

	return buf;
}

int cdir(char strname[])
{
	int fi,inum,bnum;
	fi=Fd_dirfile(strname);//查找该文件夹下是否有文件
	if(fi!=-1)
	{
		printf("该文件夹下已有 %s 文件或文件夹\n",strname);
		return 0;
	}


	inum=ialloc();//申请inode空间
	if(inum<=-1)
	{
		printf("inode 申请失败!\n");
		return 0;
	}


	bnum=balloc(1);//申请block
	if(bnum<=-1)
	{
		printf("block 申请失败!\n");
		//释放申请成功的inode
		di_bitmap[inum]=0;//inode位图置0 表示空闲
		filsys.s_free_inodes_count++;
		return 0;
	}

	inodetemp->di_ino=inum;//i节点标志
	//printf("inum=%d\n",inum);
	inodetemp->di_number=2;//关联一个文件
	inodetemp->di_mode=DIMODE_DIR;//
	inodetemp->di_uid=cur_user->p_uid;//用户id
	inodetemp->di_gid=cur_user->p_gid;//组id
	inodetemp->di_size=0;//
	inodetemp->di_ctime=0;   /* Creation time */
	inodetemp->di_mtime=0;   /* Modification time */
	inodetemp->di_block[0]=bnum;//所占物理块号 后3块分别是 一级指针，二级指针，3级指针

	fseek(fd,DINODESTART+BLOCKSIZ*inum, SEEK_SET);//将新建的文件写入硬盘inode区
	fwrite(inodetemp,BLOCKSIZ, 1, fd);

	struct direct buf[BLOCKSIZ / sizeof(struct direct)];

	strcpy(buf[0].d_name,".");
	buf[0].d_ino= inum;//当前目录的dinode号
	strcpy(buf[1].d_name,"..");
	buf[1].d_ino= cur_inode->di_ino;

	fseek(fd,DATASTART+BLOCKSIZ*bnum, SEEK_SET);//将新建的文件写入硬盘data区
	fwrite(buf,BLOCKSIZ, 1, fd);


	dir_buf[cur_inode->di_number].d_ino=inum;//修改当前目录的结构
	strcpy(dir_buf[cur_inode->di_number].d_name,strname);

	fseek(fd,DATASTART+BLOCKSIZ*cur_inode->di_block[0], SEEK_SET);//将当前目录信息写入文件的block区
	fwrite(dir_buf,BLOCKSIZ, 1, fd);

	cur_inode->di_number++;//当前目录关联文件数++
	printf("当前目录文件数%d\n",cur_inode->di_number);


	fseek(fd,DINODESTART+BLOCKSIZ*cur_inode->di_ino, SEEK_SET);//将当前目录信息写入文件的inode区
	fwrite(cur_inode,sizeof(struct inode), 1, fd);


	filsys.s_free_blocks_count--;
	filsys.s_free_blocks_group[inodetemp->di_block[0]/GROUPNUM]--;
	filsys.s_free_inodes_count--;


	fseek(fd, BLOCKSIZ, SEEK_SET);//修改超级块 free block计数 free inode计数 和每组free block 计数
	fwrite (&filsys,BLOCKSIZ, 1,fd);

	fseek(fd, BLOCKSIZ*2, SEEK_SET);//在硬盘修改inode位图
	fwrite (di_bitmap,BLOCKSIZ, 1,fd);

	fseek(fd, BLOCKSIZ*3, SEEK_SET);//在硬盘修改block位图
	fwrite (bk_bitmap,BLOCKSIZ, 1,fd);

	return 1;

}
int adduser(char strname[])
{
	int fi,inum,bnum;
	if(strcmp(cmdhead,"root\\etc")!=0){
        printf("需要在root\\etc目录下创建\n");
        return 0;
	}
	fi=Fd_dirfile(strname);//查找该文件夹下是否有文件
	if(fi!=-1)
	{
		printf("该文件夹下已有 %s 文件或文件夹\n",strname);
		return 0;
	}


	inum=ialloc();//申请inode空间
	if(inum<=-1)
	{
		printf("inode 申请失败!\n");
		return 0;
	}


	bnum=balloc(1);//申请block
	if(bnum<=-1)
	{
		printf("block 申请失败!\n");
		//释放申请成功的inode
		di_bitmap[inum]=0;//inode位图置0 表示空闲
		filsys.s_free_inodes_count++;
		return 0;
	}
	fseek(fd, DATASTART+BLOCKSIZ*2, SEEK_SET);
	fread(pwd,BLOCKSIZ, 1, fd);
    for (usernum=0; pwd[usernum].p_uid != 0; usernum++);
    if(usernum>=PWDNUM){
        printf("用户总数不能超过%d",PWDNUM);
        return 0;
    }
	inodetemp->di_ino=inum;//i节点标志
	inodetemp->di_number=2;//关联一个文件
	inodetemp->di_mode=DIMODE_DIR|DIMODE_SYSTEM;//
	inodetemp->di_uid=usernum;//用户id
	inodetemp->di_gid=cur_user->p_gid;//组id
	inodetemp->di_size=0;//
	inodetemp->di_ctime=0;   /* Creation time */
	inodetemp->di_mtime=0;   /* Modification time */
	inodetemp->di_block[0]=bnum;//所占物理块号 后3块分别是 一级指针，二级指针，3级指针

	fseek(fd,DINODESTART+BLOCKSIZ*inum, SEEK_SET);//将新建的文件写入硬盘inode区
	fwrite(inodetemp,BLOCKSIZ, 1, fd);

	struct direct buf[BLOCKSIZ / sizeof(struct direct)];

	strcpy(buf[0].d_name,".");
	buf[0].d_ino= inum;//当前目录的dinode号
	strcpy(buf[1].d_name,"..");
	buf[1].d_ino= cur_inode->di_ino;

	fseek(fd,DATASTART+BLOCKSIZ*bnum, SEEK_SET);//将新建的文件写入硬盘data区
	fwrite(buf,BLOCKSIZ, 1, fd);


	dir_buf[cur_inode->di_number].d_ino=inum;//修改当前目录的结构
	strcpy(dir_buf[cur_inode->di_number].d_name,strname);

	fseek(fd,DATASTART+BLOCKSIZ*cur_inode->di_block[0], SEEK_SET);//将当前目录信息写入文件的block区
	fwrite(dir_buf,BLOCKSIZ, 1, fd);

	cur_inode->di_number++;//当前目录关联文件数++
	printf("当前目录文件数%d\n",cur_inode->di_number);


	fseek(fd,DINODESTART+BLOCKSIZ*cur_inode->di_ino, SEEK_SET);//将当前目录信息写入文件的inode区
	fwrite(cur_inode,sizeof(struct inode), 1, fd);


	filsys.s_free_blocks_count--;
	filsys.s_free_blocks_group[inodetemp->di_block[0]/GROUPNUM]--;
	filsys.s_free_inodes_count--;


	fseek(fd, BLOCKSIZ, SEEK_SET);//修改超级块 free block计数 free inode计数 和每组free block 计数
	fwrite (&filsys,BLOCKSIZ, 1,fd);

    strcpy(pwd[usernum].username,strname);
    printf("input the password for %s:\n",strname);
    scanf("%s",&pwd[usernum].password);
    pwd[usernum].p_uid= usernum;
    pwd[usernum].p_gid = GRUP_1;
    fseek(fd,DATASTART+BLOCKSIZ*2, SEEK_SET);
    fwrite(pwd,BLOCKSIZ,1,fd);

	fseek(fd, BLOCKSIZ*2, SEEK_SET);//在硬盘修改inode位图
	fwrite (di_bitmap,BLOCKSIZ, 1,fd);

	fseek(fd, BLOCKSIZ*3, SEEK_SET);//在硬盘修改block位图
	fwrite (bk_bitmap,BLOCKSIZ, 1,fd);

	return 1;

}
//显示inode位图 block位图 的使用情况 1表示已用 0表示未用
void showbitmap()
{
	int i;
	printf("inode free number %d,block free number %d\n",filsys.s_free_inodes_count,filsys.s_free_blocks_count);
	for(i=0;i<DINODENUM;i++)
	{
		printf("%d",di_bitmap[i]);

	}
	for(i=0;i<DATABLKNUM;i++)
	{
		if(i%BLKGRUPNUM==0)
		{
			printf("\n");
		}
		printf("%d",bk_bitmap[i]);
	}
	printf("\n");
}

int deletefd(char strname[20])
{
	int fi,i;
	fi=Fd_dirfile(strname);//查找该文件夹下是否有文件
	if(fi==-1)
	{
		printf("该文件夹下没有 %s 文件或文件夹\n",strname);
		return 0;
	}
	inodetemp=read_inode(fi);

	if((inodetemp->di_mode&DIMODE_SYSTEM)==DIMODE_SYSTEM)
	{
		printf("%s是系统文件，无法删除\n",strname);
		return 0;
	}else	if((inodetemp->di_mode&DIMODE_FILE)==DIMODE_FILE)//如果是文件 则直接删除
	{

	}
	else if((inodetemp->di_mode&DIMODE_DIR)==DIMODE_DIR)
	{
		if(inodetemp->di_number>2)
		{
			printf("该目录下还有文件，不能删除此文件夹!\n");
			return 0;
		}
	}

	if(!myaccess())
	{
		printf("您没有权限!\n");
		return -1;
	}


	 char c;
	 printf("是否真的要删除 %s ?<y/n>",strname);
	 fgetc(stdin);
	 scanf("%c",&c);
	 if(c!='y')
	 {
		 return 0;
	 }

	 i=0;


	 while(strcmp(dir_buf[i].d_name,strname)!=0)
	 {
		 i++;
	 }
	 for(;i<cur_inode->di_number;i++)
	 {
		 strcpy(dir_buf[i].d_name,dir_buf[i+1].d_name);
		 dir_buf[i].d_ino=dir_buf[i+1].d_ino;
	 }
	 cur_inode->di_number--;

	 fseek(fd,DATASTART+BLOCKSIZ*cur_inode->di_block[0], SEEK_SET);//更新当前目录block
	 fwrite(dir_buf,BLOCKSIZ, 1, fd);

	 fseek(fd,DINODESTART+BLOCKSIZ*cur_inode->di_ino, SEEK_SET);//更新当前目录inode
	 fwrite(cur_inode,BLOCKSIZ, 1, fd);

	 for(i=0;i<(int)(inodetemp->di_size/BLOCKSIZ+1);i++)
	 {
		bk_bitmap[inodetemp->di_block[i]]=0;
	 }

	 di_bitmap[inodetemp->di_ino]=0;

	 filsys.s_free_blocks_count++;
	 filsys.s_free_blocks_group[inodetemp->di_block[0]/GROUPNUM]++;
	 filsys.s_free_inodes_count++;


	 fseek(fd, BLOCKSIZ, SEEK_SET);//修改超级块 free block计数 free inode计数 和每组free block 计数
	 fwrite (&filsys,BLOCKSIZ, 1,fd);

	 fseek(fd, BLOCKSIZ*2, SEEK_SET);//在硬盘修改inode位图
	 fwrite (di_bitmap,BLOCKSIZ, 1,fd);

	 fseek(fd, BLOCKSIZ*3, SEEK_SET);//在硬盘修改block位图
	 fwrite (bk_bitmap,BLOCKSIZ, 1,fd);

	return 1;
}


int mywrite(char strname[])//编辑文件
{
	int fi,i,k;
	char *buf;
	char *buf1;
	fi=Fd_dirfile(strname);//查找该文件夹下是否有文件
	if(fi==-1)
	{
		printf("该文件夹下没有 %s 文件或文件夹\n",strname);
		return 0;
	}
	inodetemp=read_inode(fi);

	if(!myaccess())
	{
		printf("您没有权限!\n");
		return NULL;
	}

	buf=(char *)malloc((inodetemp->di_size/BLOCKSIZ+1)*BLOCKSIZ);
	buf1=(char *)malloc(BLOCKSIZ);
	buf=ReadFile(strname);
	if(buf==NULL)
	{
		printf("文件读取失败!\n");
		return 0;
	}
     char c;
	 printf("是否真的要写覆盖 %s ?<y/n>",strname);
	 fgetc(stdin);
	 scanf("%c",&c);
     if(c!='y')
	 {
		 return 0;
	 }
	i =0;
	k=0;

	printf("原来文件的内容%s\n",buf);
	printf("输入新文件内容，以\"###\"结束:\n");
	fgetc(stdin);
	while(k!=3)
	{
		buf1[i] = getchar();
		if(buf1[i] == '#')
		{
			k++;
			if(k == 3)
				break;
		}
		else
			k=0;
		i++;
		if(i/BLOCKSIZ > 0)
		{
			buf1 = (char *)realloc(buf1,BLOCKSIZ*(i/BLOCKSIZ+1));
		}
	}
	buf1[i-2]='\0';


	inodetemp->di_size=strlen(buf1);
	fseek(fd,DINODESTART+BLOCKSIZ*inodetemp->di_ino, SEEK_SET);//将新建的文件写入硬盘inode区
	fwrite(inodetemp,BLOCKSIZ, 1, fd);

	fseek(fd, DATASTART+BLOCKSIZ*inodetemp->di_block[0], SEEK_SET);
	fwrite (buf1, BLOCKSIZ*(inodetemp->di_size/BLOCKSIZ+1),1,fd);


	return 1;

}


void login()
{
	char str[20];
	int i;
	do{
		do{
		printf("user name:");
		fflush(stdin);
		scanf("%s",str);
		for(i=0;i<PWDNUM ;i++)
		{
		//printf("%s",pwd[i].username);
			if(strcmp(pwd[i].username,str)==0)
			{
				break;
			}
			if(strcmp("exit",str)==0)
			{
				exit(0);
			}
		}
		if(i!=PWDNUM)
		{
			break;
		}

		}while(1);
	printf("passwd:");
	fgetc(stdin);
	scanf("%s",str);
	if(strcmp(pwd[i].password,str)==0)
	{
		break;
	}
	if(strcmp("exit",str)==0)
	{
		exit(0);
	}

	}while(1);

cur_user=&pwd[i];

inodetemp=read_inode(0);//读取主目录的inode;
if((inodetemp->di_mode&DIMODE_DIR)!=DIMODE_DIR)
{
	printf("读取主目录失败,请重新格式化!\n");
}
else
{
	changeinode();//交换指针    cur_inode 指向当前目录的 inode
	read_dir_data(cur_inode->di_block[0]);

	//printf("%d",cur_inode->di_block[0]);
}

strcpy(cmdhead,"root");
cmd_Up("cd","etc");
cmd_Up("cd",cur_user->username);
printf("login success!!\n");

}

void showhelp()//显示命令帮助
{
	printf("\thelp\t\t显示命令帮助\n");
	printf("\tdir\t\t显示当前目录下的文件和文件夹\n");
	printf("\tbit\t\t显示inode block的占用情况\n");
	printf("\tcd [目录名]\t进入一个目录\n");
	printf("\tcdir [目录名]\t创建一个目录\n");
	printf("\tcreate [文件名]\t创建一个文本文件\n");
	printf("\tread [文件名]\t读一个已经存在的文本文件\n");
	printf("\twrite [文件名]\t写一个已经存在的文本文件\n");
	printf("\tdel [文件名]\t删除文件\n");
	printf("\tadduser [文件名]增加一个用户\n");
	printf("\texit\t\t退出系统\n");
}

int myaccess()
{

	if(inodetemp->di_uid==cur_user->p_uid)//如果是该用户创建的 则有读写权限
	{
		return 1;
	}
	else	if(cur_user->p_gid==0)//如果当前用户是管理员组的 也具有读写权限
	{
		return 1;
	}
	else if(inodetemp->di_gid==1)//如果该文件是用户可查看文件 则都具有权限
	{
		return 1;	
	}
	return 0;
}
