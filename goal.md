很好，这一步你思路选对了 👍
👉 **既然你已经维护 parent_id，那 delete 可以写成“标准工业版 B+Tree”**，而且确实能复用你 insert 的很多结构（尤其是 split 的对称操作）。

我给你讲一个**可以直接落代码的 delete 思路（贴近你现在代码风格）**👇

---

# 🧠 一句话总流程

```text
FindLeaf → 删除 key →
如果没 underflow：结束
否则：
  borrow（优先） or merge →
  向上递归修复 →
  可能更新 root
```

---

# 🌲 一、整体结构（你就按这个写）

```cpp
void Remove(const KeyType &key) {
  if (IsEmpty()) return;

  Context ctx;

  // 1. 找 leaf（和 insert 一样）
  page_id_t leaf_id = FindLeaf(key, ctx);
  auto &leaf_guard = ctx.write_set_.back();
  auto leaf = leaf_guard.AsMut<LeafPage>();

  // 2. 删除 key
  int index;
  if (!leaf->BinarySearchForIndex(key, comparator_, index)) {
    return;  // 不存在
  }

  leaf->RemoveAt(index);  // 你需要实现

  // 3. 如果是 root 特判
  if (ctx.write_set_.size() == 1) {
    AdjustRootAfterDelete(ctx);
    return;
  }

  // 4. 如果没 underflow，结束
  if (leaf->GetSize() >= leaf->GetMinSize()) {
    return;
  }

  // 5. 处理 underflow
  CoalesceOrRedistribute(ctx);
}
```

---

# 🔥 二、核心：underflow 怎么处理

---

## 📌 两种策略（必须按顺序）：

```text
1. borrow（兄弟借一个） ← 优先
2. merge（合并）       ← borrow 不行才用
```

---

# 🌿 三、如何找 sibling（你现在可以用 parent_id 了）

---

## 当前：

```text
ctx.write_set_:
[root ... parent leaf]
```

---

## 👉 parent：

```cpp
auto &parent_guard = ctx.write_set_[ctx.write_set_.size() - 2];
auto parent = parent_guard.AsMut<InternalPage>();
```

---

## 👉 找 leaf 在 parent 的 index：

```cpp
int idx = parent->ValueIndex(leaf_guard.GetPageId());
```

---

## 👉 sibling：

```cpp
left  = idx > 0
right = idx < parent->GetSize()
```

---

# 🔥 四、borrow（推荐优先实现）

---

## 👉 从左借：

```text
[left sibling] → leaf
```

---

### 叶子节点：

```cpp
// 把左兄弟最后一个搬过来
leaf->InsertAt(0, left->KeyAt(last), left->RidAt(last));
left->RemoveAt(last);

// 更新 parent key
parent->SetKeyAt(idx, leaf->KeyAt(0));
```

---

---

## 👉 从右借：

```text
[right sibling] → leaf
```

---

```cpp
leaf->InsertAt(end, right->KeyAt(0), right->RidAt(0));
right->RemoveAt(0);

// 更新 parent key
parent->SetKeyAt(idx + 1, right->KeyAt(0));
```

---

---

## ❗ 条件：

```text
sibling->GetSize() > sibling->GetMinSize()
```

---

# 🔥 五、merge（borrow 不行才做）

---

## 👉 merge leaf + right（推荐只实现一种方向）

```text
leaf + right → leaf
```

---

```cpp
// 把 right 全部搬到 leaf
for (...) {
  leaf->InsertAt(...);
}

// 修链表
leaf->SetNextPageId(right->GetNextPageId());
```

---

---

## 👉 然后删除 parent 中的 key

```cpp
parent->RemoveAt(idx + 1);
```

---

👉 ⚠️ 这里非常关键：

```text
internal 删除的是 key + pointer
```

---

---

## 👉 如果 parent underflow：

```cpp
CoalesceOrRedistribute(ctx);  // 递归
```

---

# 🌲 六、Internal 节点的处理（和 leaf 类似但更 tricky）

---

## borrow：

👉 不只是 key，还要带 child pointer

---

### 从左借：

```text
parent key 下移
left sibling key 上移
```

---

### 从右借：

同理

---

---

## merge：

```text
把 parent key 插入中间
再合并两个 internal
```

---

---

👉 示例：

```text
parent:   [ k ]
         /     \
      left     right
```

merge 后：

```text
left: [ ... k ... ]
```

---

# 🔥 七、root 特判（非常重要）

---

## 情况 1：root 是 leaf

```text
删空 → 整棵树空
```

---

## 情况 2：root 是 internal 且 size=1

```text
root collapse
```

---

```cpp
new_root = root->ValueAt(0);
header->root_page_id_ = new_root;
```

---

# 🧠 八、你可以复用 insert 的地方

---

## ✔ 可以复用：

* `FindLeaf` ✔
* `BinarySearch` ✔
* `InsertMapping`（borrow 用）✔
* `SplitInternal` 的“对称理解” ✔

---

## ❗ 需要新增：

```text
RemoveAt
CoalesceOrRedistribute
BorrowFromLeft / Right
Merge
AdjustRootAfterDelete
```

---

# 🎯 九、推荐实现顺序（非常关键）

别一口气写完，会炸。

---

## 🟢 Step 1：

```text
只实现：
删除 + 不 underflow
```

---

## 🟢 Step 2：

```text
只实现：
borrow（不 merge）
```

---

## 🟢 Step 3：

```text
实现 merge
```

---

## 🟢 Step 4：

```text
实现 root collapse
```

---

# 🚀 十、一句话总结

```text
delete = insert 的镜像操作
但更复杂（因为有两种修复策略）
```

---

# 💬 如果你下一步要写

我可以直接帮你：

👉 写一版 **CoalesceOrRedistribute（完整可用代码）**

这是 delete 最核心、最容易写崩的函数。
