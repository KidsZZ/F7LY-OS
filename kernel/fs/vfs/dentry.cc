#include "fs/vfs/dentrycache.hh"
#include "fs/vfs/inode.hh"
#include "fs/vfs/dstat.hh"

#include "fs/fat/fat32inode.hh"

#include "fs/ramfs/ramfsInode.hh"
#include "fs/ramfs/ramfs.hh"
#include "printer.hh"


#include <EASTL/queue.h>

namespace fs
{
	dentry::~dentry()
	{
		delete _node;
	}

	fs::dentry *dentry::EntrySearch( const eastl::string name )
    {
        printfBlue("dentry::EntrySearch: name = %s\n", name.c_str());
        auto it = children.find(name);
        if (it != children.end())
            return it->second;
        // printfRed("dentry::EntrySearch: name %s not found in children\n", name.c_str());
        if ( [[maybe_unused]] auto subnod = (_node->lookup(name)))
        {
            // printfBlue("dentry::EntrySearch: subnod found with name = %s", name.c_str());
            dentry *subdentry = fs::dentrycache::k_dentryCache.alloDentry();
            if( subdentry == nullptr )
            {
                printfRed("dentry::EntrySearch: Failed to create dentry");
                return nullptr;
            }
            if(name.empty())
            {
                printfRed("dentry::EntrySearch: name is empty");
                return nullptr;
            }
            new ( subdentry ) dentry(name, (Inode *)subnod, this);
            // printfBlue("dentry::EntrySearch: subdentry created with name = %s", name.c_str());
            //dentry *subdentry = new dentry(name , subnod, this);
            children[name] = subdentry;
            return subdentry;
        }
        printfRed("dentry::EntrySearch: name %s not found in children\n", name.c_str());
        return nullptr;
    }

    fs::dentry *dentry::EntryCreate( eastl::string name, FileAttrs attrs, eastl::string dev_name )
    {
        // printfBlue("dentry::EntryCreate: name = %s, attrs = %d, dev_name = %s", name.c_str(), attrs.transMode(), dev_name.c_str());
        if (name.empty()) {
            printfRed("dentry::EntryCreate: name is empty");
            return nullptr;
        }

            // check if the name already exists
        if (children.find(name) != children.end()) {
            printfRed("dentry::EntryCreate: name already exists");
            return nullptr;
        }

        //[[maybe_unused]] FileSystem *fs = node->getFS();

        Inode* node_ = this->_node->mknode( name, attrs, dev_name );
        if (node_ == nullptr) 
        {
            printfRed("dentry::EntryCreate: nodefs is not RamFS");
            return nullptr;
        }
        // printfBlue("dentry::EntryCreate: node created with name = %s", name.c_str());
        //dentry *newden = new dentry( name, node_, this );
        dentry *newden = fs::dentrycache::k_dentryCache.alloDentry();
        if (newden == nullptr) {
            printfRed("dentry::EntryCreate: Failed to create dentry");
            delete node_; // 避免内存泄漏
            return nullptr;
        }
        new ( newden ) dentry( name, node_, this );
        if ( newden == nullptr ) {
            printfRed("dentry::EntryCreate: Failed to create RamFSDen");
            delete node_; // 避免内存泄漏
            return nullptr;
        }

        children [ name ] = newden;

        // Info("RamFSDen::EntryCreate: created %s, parent %s", name.c_str(), this->name.c_str());
        return newden;
    }

    void dentry::reset( eastl::vector<int> &bitmap )
    {
        bitmap[ Did ] = 0;
		parent->delete_child( name ); // delete from parent
		parent = nullptr;
		_node = nullptr;
		name.clear();
		for( auto &p : children )
		{
			p.second->reset( bitmap );
		}
		children.clear();

    }

    Inode *dentry::getNode()
    {
        return _node;
    }

    bool dentry::isRoot()
    {
        return this == _node->getFS()->getRoot();
    }

    bool dentry::isMntPoint()
    {
        return this == _node->getFS()->getMntPoint();
    }

    void dentry::printChildrenInfo( )
    {
        eastl::queue<dentry*> current;
        dentry *den;
        FileAttrs attrs;
        current.push(this); // 将当前节点添加到队列
            
        while (!current.empty()) {
            den = current.front(); // 获取队列前端的节点
            current.pop(); 
            attrs = den->getNode()->rMode();

            //printf("dentry name is %s, and it's attr is %d, it is a %d file", den->rName().c_str(), attrs.transMode(), attrs.filetype);            
            printf("dentry name is %s\n", den->rName().c_str());
            eastl::unordered_map<eastl::string, dentry*> children = den->getChildren();
            for (auto &child : children) {
                current.push(child.second); // 将子节点添加到队列中以便后续处理
            }
            }
    }

    void dentry::printAllChildrenInfo(const eastl::string &prefix, bool isLast)
    {
        // 打印当前节点
        printf("%s", prefix.c_str());
        if (!prefix.empty())
        {
            printf("%s", isLast ? "└── " : "├── ");
        }

        FileAttrs attrs = _node->rMode();
        printf("%s [mode: %d, type: %d]\n", name.c_str(), attrs.transMode(), attrs.filetype);

        // 准备新的前缀
        eastl::string newPrefix = prefix;
        if (!prefix.empty())
        {
            newPrefix += isLast ? "    " : "│   ";
        }

        size_t idx = 0;
        size_t total = children.size();
        for (auto it = children.begin(); it != children.end(); ++it, ++idx)
        {
            bool lastChild = (idx == total - 1);
            it->second->printAllChildrenInfo(newPrefix, lastChild);
        }
    }

    void dentry::unlink()
    {
        /// @todo node->unlink(); linkcnt--;
    }

    int dentry::readDir( Dstat *dst, size_t off, size_t len )
    {
        /// @todo : readDir
        return 0; 
    }
}// namespace fs