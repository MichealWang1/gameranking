#pragma once
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <random>

// std::atomic 原子操作
// std::mt19937 随机值

// 定义跳表节点模板，支持任意类型的键值对
template<typename K, typename V>
struct Node
{
    K m_stKey;// 节点的键
    V m_stValue;// 节点的值
    std::atomic<Node<K, V>**>    m_pstForward;// 指向下一个节点的原子指针数组，用于实现多层链表
    std::atomic<int>            m_iTopLevel;// 节点的最高层级
    std::atomic<bool>         m_bMarked;// 标记节点是否被删除
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

// 定义跳表模板类，实现高效的插入、删除和查找操作
template<typename K, typename V>
class SkipList
{
    private:
        const int MAXLEVEL;           // 跳表的最大层级
        const float PROBABILITY;    // 随机层级的概率因子
        std::atomic<int> m_iCurrentLevel;// 当前跳表的最高层级
        Node<K, V> m_stHead;         // 头节点指针
        Node<K, V> m_stTail;            // 尾节点指针
        std::mt19937    m_iRang;        // Mersenne Twister随机数生成器

        // 查找指定键的节点，并记录前驱和后继节点
        bool FindNode(K key, Node<K,V>** preds, Node<K,V>** succs) 
        {
            int bottomLevel = 0;// 最低层级为0
            bool marked = false;// 标记节点是否被删除
            bool snip = false;// 标记是否成功删除标记节点
            Node<K, V>* pred = nullptr;
            Node<K, V>* curr = nullptr;
            Node<K, V>* succ = nullptr;
            
        retry:
            while (true)
            {
                // 从头节点开始
                pred = m_stHead;

                // 从最高层向下遍历
                for (int level = m_iCurrentLevel; level >= bottomLevel; --level) 
                {
                    // 获取当前层级的下一个节点
                    curr = pred->m_pstForward[level];

                    // 查找当前层级的合适位置
                    while (true)
                    {
                        // 获取后继节点
                        succ = curr->m_pstForward[level];
                        // 检查当前节点是否被标记删除
                        marked = curr->m_bMarked.load();
                        if (marked)
                        { // 如果节点被标记删除
                            // 尝试删除标记节点
                            snip = pred->m_pstForward[level].compare_exchange_strong(curr, succ);
                            if (!snip) 
                            {
                                // 删除失败则重试
                                goto retry;
                            }

                            // 删除成功 更新当前节点
                            curr = pred->m_pstForward[level];
                        }
                        else 
                        {
                            // 如果当前节点键 小于 目标键
                            if (curr->m_stKey < key) 
                            {
                                // 移动前驱节点
                                pred = curr;
                                // 移动当前节点
                                curr = succ;
                            }
                            else 
                            {
                                // 已经找到合适位置，退出内层循环
                                break;
                            }
                        }
                    }

                    // while循环结束表示就是找到了合适位置插入
                    preds[level] = pred;
                    succs[level] = succ;
                }
                return curr->m_stKey == key;
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

    // 初始化随机数生成器
    ~SkipList() 
    {
        // 从头节点开始
        Node<K, V>* curr = m_stHead;
        // 遍历所有节点
        while (curr != m_stTail)
        {
            // 获取下一个节点
            Node<K, V>* next = curr->m_pstForward[0];
            // 释放当前节点内存
            delete curr;
            // 移动到下一个节点
            curr = next;
        }
        // 释放尾节点内存
        delete m_stTail;
    }



};

