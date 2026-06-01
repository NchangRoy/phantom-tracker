#include <stdio.h>
#include <stdlib.h>
#include"linkedlist.h"

Node *create_node(void *data) {
    Node *node = (Node *)malloc(sizeof(Node));
    if (!node) return NULL;

    node->data = data;
    node->next = NULL;

    return node;
}


void push_back(Node **head, void *data) {
    Node *node = create_node(data);

    if (*head == NULL) {
        *head = node;
        return;
    }

    Node *temp = *head;
    while (temp->next != NULL)
        temp = temp->next;

    temp->next = node;
}


void print_nodes(Node * head,void (*print_node)(void *)){
    Node * temp=head;
    while(temp!=NULL){
        print_node(temp->data);
        temp=temp->next;
    }
}