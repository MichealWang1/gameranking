#pragma once
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <random>

// std::atomic 原子操作
// std::mt19937 随机值

// 跳表节点模板类，支持任意类型的键值对
template<typename K, typename V>
struct Node
{
    K m_stKey;  // 节点键值，用于排序
    V m_stValue;// 节点存储的值
    std::atomic<Node<K, V>**>    m_pstForward;// 指向下一个节点的原子指针数组，支持多层级索引
    std::atomic<int>            m_iTopLevel;// 节点的最高层级，随机生成
    std::atomic<bool>         m_bMarked;    // 标记节点是否被删除(逻辑删除)
    std::atomic<bool>         m_bFullyLinked;// 标记节点是否已完全链接到跳表中
    char                                m_chPadding[64];// 内存对齐填充，减少伪共享
    
    // 节点构造函数
    Node(K k, V v, int level) : m_stKey(k), m_stValue(v), m_bMarked(false), m_bFullyLinked(false) 
    {
        m_pstForward = new Node<K, V>* [level + 1];// 分配层级+1个指针空间
        for (int i = 0; i <= level; ++i)
        {
            // 初始化各层级的指针为空
            m_pstForward.load()[i] = nullptr;
        }

        // 设置节点的最高层级
        m_iTopLevel = level;
    }

    // 节点析构函数
    ~Node() 
    {
        // 释放指针数组内存
        delete[] m_pstForward;
    }
};

// 跳表模板类，实现无锁的高效插入、删除和查找操作
template<typename K, typename V>
class SkipList
{
    private:
        const int MAXLEVEL;           // 跳表的最大层级限制
        const float PROBABILITY;    // 随机层级生成的概率因子
        std::atomic<int> m_iCurrentLevel;// 当前跳表的实际最高层级
        Node<K, V> m_stHead;         // 头节点指针
        Node<K, V> m_stTail;            // 尾节点指针
        std::mt19937    m_iRang;        // Mersenne Twister随机数生成器

		// 查找指定键的节点，并记录路径上前驱和后继节点
        bool FindNode(K key, Node<K,V>** preds, Node<K,V>** succs)
        {
            int iBottomLevel = 0;// 最低层级为0
            bool bMarked = false;// 标记节点是否被删除
            bool bSnip = false;// 标记是否成功删除标记节点
            Node<K, V>* pstPredNode = nullptr;
            Node<K, V>* pstCurrNode = nullptr;
            Node<K, V>* pstSuccNode = nullptr;
            
        retry:
            while (true)
            {// 外层循环，可能需要重试
                // 从头节点开始遍历
                pstPredNode = m_stHead;
                // 从最高层向下遍历
                for (int level = m_iCurrentLevel; level >= iBottomLevel; --level)
                {
                    // 获取当前层级的下一个节点
                    pstCurrNode = pstPredNode->m_pstForward[level];
                    // 内层循环，查找当前层级的合适位置
                    while (true)
                    {
                        // 获取后继节点
                        pstSuccNode = pstCurrNode->m_pstForward[level];
                        // 检查当前节点是否被标记删除
                        bMarked = pstCurrNode->m_bMarked.load();
                        if (bMarked)
                        { // 如果节点被标记删除
                            // 尝试原子删除标记节点
                            bSnip = pstPredNode->m_pstForward[level].compare_exchange_strong(pstCurrNode, pstSuccNode);
                            if (!bSnip)
                            {
                                // 删除失败则重试
                                goto retry;
                            }

                            // 删除成功 更新当前节点
                            pstCurrNode = pstPredNode->m_pstForward[level];
                        }
                        else 
                        {
                            // 如果当前节点键 小于 目标键
                            if (pstCurrNode->m_stKey < key)
                            {
                                // 移动前驱节点
                                pstPredNode = pstCurrNode;
                                // 移动当前节点
                                pstCurrNode = pstSuccNode;
                            }
                            else 
                            {
                                // 已经找到合适位置，退出内层循环
                                break;
                            }
                        }
                    }
                    // 记录当前层级的前驱节点
                    preds[level] = pstPredNode;
                    // 记录当前层级的后继节点
                    succs[level] = pstSuccNode;
                }
                // 返回是否找到目标键的节点
                return pstCurrNode->m_stKey == key;
            }
        }

public:
	// 跳表构造函数，初始化头节点和尾节点
    SkipList(int iMaxLevel = 32, float fProbability = 0.5) : MAXLEVEL(iMaxLevel), PROBABILITY(fProbability), m_iCurrentLevel(1)
    {
        // 创建尾节点
        m_stTail = new Node<K, V>(K(), V(), MAXLEVEL);
        // 创建头节点
        m_stHead = new Node<K, V>(K().V(), MAXLEVEL);
        for (int i = 0; i <= MAXLEVEL; ++i) 
        {
            // 初始化头节点各层级指针指向尾节点
            m_stHead->m_pstForward[i] = m_stTail;
        }
        // 随机数设备
        std::random_device rd;
        // 初始化随机数生成器
        m_iRang = std::mt19937(rd());
    }

	// 跳表析构函数，释放所有节点内存
    ~SkipList()
    {
        // 从头节点开始
        Node<K, V>* pstCurr = m_stHead;
        // 遍历所有节点
        while (pstCurr != m_stTail)
        {
            // 获取下一个节点
            Node<K, V>* pstNext = pstCurr->m_pstForward[0];
            // 释放当前节点内存
            delete pstCurr;
            // 移动到下一个节点
            pstCurr = pstNext;
        }
        // 释放尾节点内存
        delete m_stTail;
    }

	// 生成随机层级，决定新节点的高度
    int RandomLevel() 
    {
        // 初始层级为1
        int iLevel = 1;
        // 均匀分布随机数
		std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        // 根据概率决定是否增加层级
        while (dist(m_iRang) < PROBABILITY && iLevel < MAXLEVEL)
        {
            // 累计层级
            iLevel++;
        }
        // 返回随机生成的层级
		return iLevel;
    }

	// 插入键值对到跳表中，使用无锁CAS操作保证线程安全
    bool Insert(K key, V value) 
    {
		int iTopLevel = RandomLevel();// 生成新节点的随机层级
		Node<K, V>* pstPreds[MAXLEVEL + 1];// 存储各层级的前驱节点
		Node<K, V>* pstSuccs[MAXLEVEL + 1];// 存储各层级的后继节点
        while (true) 
        {
            // 循环直到插入成功
            if (FindNode(key, pstPreds, pstSuccs))
            {
                // 如果键已存在  获取找到的节点
                Node<K, V>* pstNodeFound = pstSuccs[0];
                // 查看节点 是否被标记 删除
                if (!pstNodeFound->m_bMarked)
                {
                    // 等待节点完全链接
                    while (!pstNodeFound->m_bFullyLinked);
                    // 更新节点值
                    pstNodeFound->m_stValue = value;
                    // 返回插入成功(更新操作)
					return true;
                }
                // 否则继续尝试
                continue;
            }

            // 创建新节点
			Node<K, V> pstNewNode = new Node<K, V>(key, value, iTopLevel);
            // 初始化新节点各层级指针
            for (int level = 0; level < iTopLevel; ++level)
            {
                // 初始化新节点各层级指针
                pstNewNode->m_bFullyLinked[level] = pstSuccs[level];
            }

			// 获取最低层级的前驱节点
			Node<K, V>* pstPred = pstSuccs[0];
            // 获取最低层级的后继节点
            Node<K, V>* pstSucc = pstSuccs[0];
            // 设置新节点的后继节点
            pstNewNode->m_pstForward[0] = pstSucc;
            // 尝试原子插入新节点
            if (!pstPred->m_pstForward[0].compare_exchange_strong(pstSucc, pstNewNode)) 
            {
                // 如果链接失败，释放新节点内存并重试
                delete pstNewNode;
                // 继续尝试
                continue;
            }

            // 处理其他层级
            for (int level = 1; level < iTopLevel; ++level) 
            {
                while (true)
                {
                    // 获取当前层级的前驱节点
                    pstPred = pstPreds[level];
                    // 获取当前层级的后继节点
					pstSucc = pstSuccs[level];
                    // 检查新节点的后继是否仍然有效 并且 尝试原子插入新节点
                    if (pstNewNode->m_pstForward[level] == pstSucc && 
                        pstPred->m_pstForward[level].compare_exchange_strong(pstSucc, pstNewNode))
                    {
                        // 插入成功，退出循环
                        break;
                    }

                    // 没有成功插入新节点  重新查找，更新前驱和后继节点
                    FindNode(key, pstPreds, pstSuccs);
                }
			}

            // 更新当前调表的最高层级
            while (m_iCurrentLevel < iTopLevel)
            {
				int iOldLevel = m_iCurrentLevel;
                // 尝试原子更新当前跳表的最高层级
                if (iOldLevel >= iTopLevel && m_iCurrentLevel.compare_exchange_strong(iOldLevel, iTopLevel))
                {
                    // 更新成功，退出循环
                    break;
				}
            }

			// 设置新节点的完全链接标志为true
            pstNewNode->m_bFullyLinked = true;
			return true;// 返回插入成功
        }
    }

	//从跳表中删除指定键的节点，使用无锁CAS操作保证线程安全
    bool Remove(K key) 
    {
		Node<K, V>* pstNodeToDelete = nullptr;// 要删除的节点
        bool bIsMarked = false;// 标记节点是否被删除
		int iBottomLevel = 0;// 最低层级为0
		Node<K, V>* pstPreds[MAXLEVEL + 1];// 存储各层级的前驱节点
        Node<K, V>* pstSuccs[MAXLEVEL + 1];// 存储各层级的后继节点
        // 循环直到删除成功或失败
        while (true)
        {
            bool bFound = FindNode(key, pstPreds, pstSuccs);// 查找目标节点
            if (bFound == false)
            {
                // 未找到目标节点，返回失败
                return false;
            }
        
			Node<K, V>* pstNodeFound = pstSuccs[0];// 获取找到的节点
            // 从最高层向下处理
            for (int level = pstNodeFound->m_iTopLevel; level >= iBottomLevel + 1; ++level)
            {
				bool bMarked = false;// 标记节点是否被删除
                // 获取当前层级的前驱动点
            }


        }
    }
};

