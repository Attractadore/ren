#pragma once

namespace ren {

template <typename T> struct ListNode {
  ListNode *prev = nullptr;
  ListNode *next = nullptr;
};

template <typename T> void list_init(ListNode<T> *node) {
  node->prev = node;
  node->next = node;
}

template <typename T> bool list_is_empty(const ListNode<T> *head) {
  return head->prev == head and head->next == head;
}

template <typename T> bool is_in_list(const ListNode<T> node) {
  return node.prev;
}

template <typename T>
void list_insert_after(ListNode<T> *prev, ListNode<T> *node) {
  ListNode<T> *next = prev->next;
  prev->next = node;
  node->prev = prev;
  node->next = next;
  next->prev = node;
}

template <typename T> void list_remove(ListNode<T> *node) {
  ListNode<T> *prev = node->prev;
  ListNode<T> *next = node->next;
  prev->next = next;
  next->prev = prev;
  *node = {};
}

}
